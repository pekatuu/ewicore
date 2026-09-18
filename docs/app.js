/* EWI5000 VA Core — WASM動作確認アプリ
 * 方針: DSPはCコア由来のWASMのみ。JSはMIDI/UIとスケジューリングだけ。
 * Audio: メインスレッドでWASM描画 → 先読みスケジューラでAudioBuffer再生。
 * (AudioWorklet内WASMの複雑化を避け、検証容易性を優先。遅延~150ms)
 */
let Mod = null, ModPico = null, ModHQ = null, AC = null;
let quality = "pico"; // pico=Pico2実機相当 / hq=PC高品位
let running = false, schedTimer = null, nextTime = 0;
const BLOCK = 512, LOOKAHEAD = 0.18;
let curNote = -1, octave = 4, breath = 0;

const $ = (id) => document.getElementById(id);
const log = (m) => { const el = $("log"); el.textContent = m; };

async function bootWasm() {
  if (Mod) return Mod;
  ModPico = await EwiModule();
  try {
    if (typeof EwiModuleHQ !== "undefined") ModHQ = await EwiModuleHQ();
  } catch (e) { ModHQ = null; }
  if (!ModHQ) {
    const s = $("selQuality");
    if (s) { [...s.options].forEach((o) => { if (o.value === "hq") o.disabled = true; }); }
  }
  Mod = quality === "hq" && ModHQ ? ModHQ : ModPico;
  if (Mod !== ModHQ && quality === "hq") {
    quality = "pico";
    const s2 = $("selQuality");
    if (s2) s2.value = "pico";
  }
  return Mod;
}

function qualityLabel() { return quality === "hq" ? "PC-HQ" : "Pico2"; }

// 音質切替。再生中は現行プリセット/パラメータ/ブレスを引き継いで再初期化する。
function setQuality(q) {
  if (q !== "pico" && q !== "hq") return;
  if (q === "hq" && !ModHQ) { $("selQuality").value = quality; return; }
  quality = q;
  Mod = q === "hq" ? ModHQ : ModPico;
  if (Mod && AC && running) {
    if (curNote >= 0) { try { Mod._ewi_noteOff(curNote); } catch (_) {} curNote = -1; }
    Mod._ewi_init(AC.sampleRate);
    Mod._ewi_program(+$("selPreset").value);
    Mod._ewi_setCutoff(+$("pCut").value);
    Mod._ewi_setBreathDepth(+$("pBCut").value);
    Mod._ewi_setReso(+$("pRes").value / 100);
    Mod._ewi_setFormant(+$("pForm").value / 100);
    Mod._ewi_setGlide(+$("pGlide").value / 1000);
    Mod._ewi_cc(1, +$("rngVib").value);
    Mod._ewi_cc(2, breath);
    Mod._ewi_setDlyMix(+$("pDlyMix").value / 100);
    Mod._ewi_setDlyTime(+$("pDlyTime").value / 1000);
    Mod._ewi_setDlyFb(+$("pDlyFb").value / 100);
    Mod._ewi_setRevMix(+$("pRevMix").value / 100);
    Mod._ewi_setRevSize(+$("pRevSize").value / 100);
    log("quality -> " + qualityLabel() + " (qflag=" + Mod._ewi_quality() + ")");
    $("btnAudio").textContent = "Audio動作中 (" + AC.sampleRate + "Hz, " + qualityLabel() + ")";
  }
}

async function startAudio() {
  try {
    await bootWasm();
    if (!AC) AC = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: "interactive" });
    await AC.resume();
    if (!Mod) throw new Error("WASMモジュールが読み込めませんでした");
    Mod._ewi_init(AC.sampleRate);
    log("WASM init sr=" + AC.sampleRate);
    window.__sharedAC = AC;
    running = true;
    nextTime = AC.currentTime + 0.1;
    schedTimer = setInterval(pump, 40);
    requestAnimationFrame(draw);
    $("btnAudio").textContent = "Audio動作中 (" + AC.sampleRate + "Hz, " + qualityLabel() + ")";
  } catch (e) {
    log("Audio開始失敗: " + (e && e.message) + " / file://直開きではなく http://localhost:8000/ で開いてください");
  }
}

// 自己テスト: キーボード/MIDIを介さずWASM→Audio経路だけを鳴らす
async function selfTest() {
  try {
    if (!running) await startAudio();
    if (!Mod || !running) throw new Error("Audioが開始できませんでした(上のメッセージを確認)");
    Mod._ewi_program(0);
    Mod._ewi_cc(2, 100);
    Mod._ewi_noteOn(69, 100);
    curNote = 69;
    setBreath(100);
    setTimeout(() => { try { Mod._ewi_noteOff(69); } catch (_) {} }, 800);
    log("selftest発音中: A4 + breath100 (800ms)。聞こえなければDAC/タブミュート/自動再生ブロックを確認");
  } catch (e) {
    log("selftest失敗: " + (e && e.message));
  }
}

function pump() {
  if (!running) return;
  // UIスライダのブレスを毎ブロック反映(滑らかさはコア内で平滑)
  while (nextTime < AC.currentTime + LOOKAHEAD) {
    Mod._ewi_render(BLOCK);
    const pL = Mod._ewi_ptrL() / 4, pR = Mod._ewi_ptrR() / 4;
    const buf = AC.createBuffer(2, BLOCK, AC.sampleRate);
    buf.getChannelData(0).set(Mod.HEAPF32.subarray(pL, pL + BLOCK));
    buf.getChannelData(1).set(Mod.HEAPF32.subarray(pR, pR + BLOCK));
    const src = AC.createBufferSource();
    src.buffer = buf;
    src.connect(AC.destination);
    src.start(nextTime);
    nextTime += BLOCK / AC.sampleRate;
  }
}

function panic() {
  if (!Mod) return;
  if (curNote >= 0) { Mod._ewi_noteOff(curNote); curNote = -1; }
  Mod._ewi_cc(2, 0); Mod._ewi_cc(123, 0);
  breath = 0; $("rngBreath").value = 0; $("valBreath").textContent = "0";
}

function noteOn(n) {
  if (!Mod || !running) return;
  if (curNote >= 0 && curNote !== n) Mod._ewi_noteOff(curNote);
  curNote = n;
  Mod._ewi_noteOn(n, 100);
  // EWI式: 音符と同時にブレスが0だと無音。確認用にブレス自動補充(弱め)
  if (breath < 10) setBreath(90);
}
function noteOff(n) {
  if (!Mod) return;
  if (curNote === n) { Mod._ewi_noteOff(n); curNote = -1; }
}

function setBreath(v127) {
  breath = v127;
  $("rngBreath").value = v127; $("valBreath").textContent = v127;
  if (Mod) Mod._ewi_cc(2, v127);
}

// --- 鍵盤UI ---
function buildKeys() {
  const names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
  const box = $("keys"); box.innerHTML = "";
  for (let i = 0; i < 12; i++) {
    const b = document.createElement("button");
    b.textContent = names[i];
    b.className = "key" + (names[i].includes("#") ? " black" : "");
    const midi = (octave + 1) * 12 + i; // oct=4 -> C4=60
    b.onpointerdown = () => noteOn(midi);
    b.onpointerup = b.onpointerleave = () => noteOff(midi);
    box.appendChild(b);
  }
}

// --- メータ ---
function draw() {
  if (!running) return;
  const cv = $("meter"), ctx = cv.getContext("2d");
  const lv = Mod ? Mod._ewi_getLevel() : 0;
  const fc = Mod ? Mod._ewi_getCutoff() : 0;
  ctx.clearRect(0, 0, cv.width, cv.height);
  ctx.fillStyle = "#111"; ctx.fillRect(0, 0, cv.width, cv.height);
  const w = Math.min(1, lv * 2) * cv.width;
  ctx.fillStyle = lv > 0.45 ? "#e5484d" : "#46a758";
  ctx.fillRect(0, 10, w, 26);
  ctx.fillStyle = "#eee"; ctx.font = "13px sans-serif";
  ctx.fillText("level " + lv.toFixed(3) + "  cutoff " + Math.round(fc) + "Hz  note " + curNote, 8, 60);
  // ブレスバー
  ctx.fillStyle = "#3e63dd";
  ctx.fillRect(0, 68, (breath / 127) * cv.width, 12);
  requestAnimationFrame(draw);
}

// --- MIDI ---
async function enableMidi() {
  try {
    const acc = await navigator.requestMIDIAccess();
    const sel = $("selMidi"); sel.innerHTML = "";
    acc.inputs.forEach((inp) => {
      const o = document.createElement("option");
      o.value = inp.id; o.textContent = inp.name || inp.id;
      sel.appendChild(o);
      inp.onmidimessage = onMidi;
    });
    if ([...acc.inputs.values()].length === 0) {
      $("midiLog").textContent = "MIDI入力なし。EWI5000のUSBを確認。";
    } else {
      $("midiLog").textContent = "MIDI受信開始。EWI5000で吹いてください。";
    }
    acc.onstatechange = () => enableMidiRefresh(acc);
  } catch (e) {
    $("midiLog").textContent = "WebMIDI不可: " + e + " (Chrome/Edge + https/localhost推奨)";
  }
}
function enableMidiRefresh(acc) {
  const sel = $("selMidi"); const cur = sel.value;
  sel.innerHTML = "";
  acc.inputs.forEach((inp) => {
    const o = document.createElement("option");
    o.value = inp.id; o.textContent = inp.name || inp.id;
    sel.appendChild(o);
    inp.onmidimessage = onMidi;
  });
  sel.value = cur;
}
function onMidi(ev) {
  const [st, d1, d2] = ev.data;
  const type = st & 0xf0;
  const pre = $("midiLog");
  const line = `in ${[...ev.data].map((v) => v.toString(16).padStart(2, "0")).join(" ")}`;
  pre.textContent = line + "\n" + pre.textContent.slice(0, 2000);
  if (!Mod || !running) return;
  Mod._ewi_midi(st, d1, d2 ?? 0);
  if (type === 0x90) { curNote = d2 === 0 ? -1 : d1; if (d2 === 0 && curNote < 0) curNote = -1; }
  else if (type === 0x80) curNote = -1;
  else if (type === 0xb0 && (d1 === 2 || d1 === 102)) { breath = d2; $("rngBreath").value = d2; $("valBreath").textContent = d2; }
  else if (type === 0xc0) { $("selPreset").value = String(d2 % 4); }
}

// --- 配線 ---
window.addEventListener("DOMContentLoaded", () => {
  // 未捕捉エラーを画面に表示 (無音報告の切り分け用)
  window.addEventListener("error", (e) => {
    const el = $("log");
    if (el) el.textContent = "window.onerror: " + (e && e.message) + " @ " + (e && e.filename) + ":" + (e && e.lineno);
  });
  buildKeys();
  $("btnAudio").onclick = startAudio;
  $("btnSelf").onclick = selfTest;
  $("btnPanic").onclick = panic;
  $("btnMidi").onclick = enableMidi;
  $("selQuality").onchange = (e) => setQuality(e.target.value);
  $("rngOct").onchange = (e) => { octave = +e.target.value; buildKeys(); };
  $("rngBreath").oninput = (e) => setBreath(+e.target.value);
  $("rngVib").oninput = (e) => {
    $("valVib").textContent = e.target.value;
    if (Mod) Mod._ewi_cc(1, +e.target.value);
  };
  $("selPreset").onchange = (e) => { if (Mod) Mod._ewi_program(+e.target.value); syncParams(+e.target.value); };
  $("pCut").oninput = (e) => { $("vCut").textContent = e.target.value; Mod && Mod._ewi_setCutoff(+e.target.value); };
  $("pBCut").oninput = (e) => { $("vBCut").textContent = e.target.value; Mod && Mod._ewi_setBreathDepth(+e.target.value); };
  $("pRes").oninput = (e) => { $("vRes").textContent = (+e.target.value / 100).toFixed(2); Mod && Mod._ewi_setReso(+e.target.value / 100); };
  $("pForm").oninput = (e) => { $("vForm").textContent = (+e.target.value / 100).toFixed(2); Mod && Mod._ewi_setFormant(+e.target.value / 100); };
  $("pGlide").oninput = (e) => { $("vGlide").textContent = (+e.target.value / 1000).toFixed(3); Mod && Mod._ewi_setGlide(+e.target.value / 1000); };
  $("pDlyMix").oninput = (e) => { $("vDlyMix").textContent = (+e.target.value / 100).toFixed(2); Mod && Mod._ewi_setDlyMix(+e.target.value / 100); };
  $("pDlyTime").oninput = (e) => { $("vDlyTime").textContent = e.target.value; Mod && Mod._ewi_setDlyTime(+e.target.value / 1000); };
  $("pDlyFb").oninput = (e) => { $("vDlyFb").textContent = (+e.target.value / 100).toFixed(2); Mod && Mod._ewi_setDlyFb(+e.target.value / 100); };
  $("pRevMix").oninput = (e) => { $("vRevMix").textContent = (+e.target.value / 100).toFixed(2); Mod && Mod._ewi_setRevMix(+e.target.value / 100); };
  $("pRevSize").oninput = (e) => { $("vRevSize").textContent = (+e.target.value / 100).toFixed(2); Mod && Mod._ewi_setRevSize(+e.target.value / 100); };

  const keymap = { a: 0, w: 1, s: 2, e: 3, d: 4, f: 5, t: 6, g: 7, y: 8, h: 9, u: 10, j: 11, k: 12 };
  const down = new Set();
  window.addEventListener("keydown", (e) => {
    if (e.repeat) return;
    if (e.code === "Space") { setBreath(110); e.preventDefault(); return; }
    const k = e.key.toLowerCase();
    if (k in keymap && !down.has(k)) {
      down.add(k);
      noteOn((octave + 1) * 12 + keymap[k]);
    }
  });
  window.addEventListener("keyup", (e) => {
    if (e.code === "Space") { setBreath(0); return; }
    const k = e.key.toLowerCase();
    if (k in keymap) { down.delete(k); noteOff((octave + 1) * 12 + keymap[k]); }
  });
});

function syncParams(p) {
  // 参考初期値表示の同期(厳密な双方向ではない)
  const table = [
    [750, 6200, 38, 35, 12], [900, 2800, 12, 15, 8], [500, 5200, 30, 45, 15], [900, 7000, 45, 25, 10],
  ][p] || [750, 6200, 38, 35, 12];
  const fx = [
    [12, 270, 32, 16, 55], [10, 300, 30, 20, 50], [10, 240, 30, 14, 50], [14, 290, 38, 18, 60],
  ][p] || [12, 270, 32, 16, 55];
  $("pCut").value = table[0]; $("vCut").textContent = table[0];
  $("pBCut").value = table[1]; $("vBCut").textContent = table[1];
  $("pRes").value = table[2]; $("vRes").textContent = (table[2] / 100).toFixed(2);
  $("pForm").value = table[3]; $("vForm").textContent = (table[3] / 100).toFixed(2);
  $("pGlide").value = table[4]; $("vGlide").textContent = (table[4] / 1000).toFixed(3);
  $("pDlyMix").value = fx[0]; $("vDlyMix").textContent = (fx[0] / 100).toFixed(2);
  $("pDlyTime").value = fx[1]; $("vDlyTime").textContent = fx[1];
  $("pDlyFb").value = fx[2]; $("vDlyFb").textContent = (fx[2] / 100).toFixed(2);
  $("pRevMix").value = fx[3]; $("vRevMix").textContent = (fx[3] / 100).toFixed(2);
  $("pRevSize").value = fx[4]; $("vRevSize").textContent = (fx[4] / 100).toFixed(2);
}

// シーケンサ(seq.js)用公開API。DSPはWASM側のみ。
window.EWI = {
  getMod: () => Mod,
  getAC: () => AC,
  isRunning: () => running,
  ensureAudio: () => startAudio(),
  midi3: (st, d1, d2) => { if (Mod && running) Mod._ewi_midi(st, d1, d2 ?? 0); },
  setBreathRaw: (v) => setBreath(v),
  uiBreath: () => breath,
  uiNote: () => curNote,
  markNote: (n) => { curNote = n; },
};
