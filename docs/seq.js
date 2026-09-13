/* seq.js — 動作確認用シーケンサ。
 * 方針: 楽曲データは同梱しない。内蔵はオリジナルの確認用フレーズのみ。
 * お手持ちのSMF(.mid)をブラウザ内で読み込み、WASMコア(Note/CC2/CC1/Bend)を
 * 駆動できる。ファイルは外部送信されずローカル処理のみ。
 *
 * タイムラインはノート発音と制御(Note/CC/Bend/Filter/Preset)を同一列で扱う。
 * item: {beat, prio, op, ...} (op: on/cc/bend/bendRaw/cut/bdepth/preset)
 */
(function () {
  const $ = (id) => document.getElementById(id);
  let song = null;          // { name, bpm, items:[...], totalBeats }
  let playing = false, timer = null, startCtx = 0, startBeat = 0, tempoRatio = 1;
  let loadedMidiName = "";

  const AXIS_DEFAULTS = { cut: 750, bdepth: 6200 };

  const stTo14 = (st) => Math.max(0, Math.min(16383, Math.round(8192 + (st / 2) * 8192)));

  // ---- オリジナル確認用フレーズ (Aマイナー基調・El Mirageではない) ----
  // 表現: scoop(しゃくり)/fall(フォール)/porta(ポルタメント)/
  //       swell(ブレスによるフィルタ開閉)/vib(ビブラート)/区間別フィルタ
  function buildDemo() {
    // [beat, dur, note, breath, opts]
    const notes = [
      // A: 落ち着いた前半 (フィルタ暗め)
      [0, 1.5, 69, 92, { scoop: -0.4, swell: 1 }],
      [2, 0.5, 72, 100, {}],
      [2.5, 0.5, 74, 104, {}],
      [3, 1.0, 76, 110, { vib: 35, swell: 1 }],
      [4, 2.0, 77, 112, { porta: 1, vib: 45, swell: 1 }],
      [6, 0.5, 76, 104, {}],
      [6.5, 0.5, 74, 100, {}],
      [7, 1.0, 72, 94, { fall: -1.5 }],
      [8, 0.5, 69, 90, { porta: 1 }],
      [8.5, 0.5, 71, 95, { porta: 1 }],
      [9, 0.5, 72, 100, { porta: 1 }],
      [9.5, 0.5, 74, 106, { porta: 1 }],
      [10, 1.5, 76, 112, { vib: 45, swell: 1 }],
      [11.5, 0.5, 74, 100, { fall: -1 }],
      [12, 1.0, 72, 98, {}],
      [13, 1.0, 69, 94, { scoop: -0.3 }],
      [14, 2.0, 67, 88, { vib: 30, swell: 1, fall: -0.5 }],
      // B: 明るい後半 (フィルタ開く)
      [16, 0.5, 67, 94, { porta: 1 }],
      [16.5, 0.5, 69, 100, { porta: 1 }],
      [17, 1.0, 72, 108, { scoop: -0.5 }],
      [18, 1.0, 74, 112, {}],
      [19, 0.5, 76, 112, { porta: 1 }],
      [19.5, 0.5, 77, 114, { porta: 1 }],
      [20, 1.5, 79, 116, { vib: 50, swell: 1 }],
      [21.5, 0.5, 77, 106, { fall: -2 }],
      [22, 1.0, 76, 110, { porta: 1 }],
      [23, 1.0, 74, 104, {}],
      [24, 0.5, 72, 100, {}],
      [24.5, 0.5, 74, 104, { porta: 1 }],
      [25, 1.0, 76, 110, {}],
      [26, 1.0, 79, 114, { scoop: -0.4, vib: 40 }],
      [27, 1.0, 77, 108, { fall: -1 }],
      [28, 3.0, 81, 118, { scoop: -0.6, vib: 55, swell: 1, fall: -2.5 }],
    ];
    const items = [
      { beat: 0, prio: 0, op: "preset", n: 0 },
      { beat: 0, prio: 0, op: "cut", hz: 480 },     // A: 暗め
      { beat: 0, prio: 0, op: "bdepth", hz: 3800 },
      { beat: 0, prio: 0, op: "cc", cc: 5, val: 42 }, // ポルタタイム ~0.13s
      { beat: 16, prio: 0, op: "cut", hz: 950 },    // B: 開く
      { beat: 16, prio: 0, op: "bdepth", hz: 7200 },
      { beat: 28, prio: 0, op: "cut", hz: 800 },
      { beat: 30, prio: 0, op: "cut", hz: 420 },    // 終止で閉じる
      { beat: 30, prio: 0, op: "bdepth", hz: 4000 },
    ];
    for (const [b, d, n, br, o] of notes) {
      const end = b + d;
      if (o.porta) {
        items.push({ beat: Math.max(0, b - 0.03), prio: 4, op: "cc", cc: 65, val: 127 });
        items.push({ beat: b + 0.12, prio: 1, op: "cc", cc: 65, val: 0 });
      }
      if (o.scoop) {
        items.push({ beat: Math.max(0, b - 0.05), prio: 4, op: "bend", st: o.scoop });
        items.push({ beat: b + 0.07, prio: 4, op: "bend", st: 0 });
      } else {
        items.push({ beat: Math.max(0, b - 0.015), prio: 2, op: "bend", st: 0 });
      }
      if (o.fall) {
        items.push({ beat: end - 0.09, prio: 4, op: "bend", st: o.fall });
        items.push({ beat: end + 0.04, prio: 2, op: "bend", st: 0 });
      }
      items.push({ beat: b, prio: 3, op: "cc", cc: 1, val: o.vib || 0 });
      if (o.swell) {
        items.push({ beat: b + d * 0.55, prio: 3, op: "cc", cc: 2, val: Math.min(127, br + 10) });
        items.push({ beat: end - 0.06, prio: 3, op: "cc", cc: 2, val: Math.max(30, br - 25) });
      }
      items.push({ beat: b, prio: 5, op: "on", note: n, vel: 100, br, dur: d, setBreath: true });
    }
    const totalBeats = 31;
    // 後始末 (フィルタはAxis既定に戻す)
    items.push({ beat: totalBeats + 0.5, prio: 1, op: "bend", st: 0 });
    items.push({ beat: totalBeats + 0.5, prio: 1, op: "cc", cc: 65, val: 0 });
    items.push({ beat: totalBeats + 0.5, prio: 1, op: "cc", cc: 1, val: 0 });
    items.push({ beat: totalBeats + 0.5, prio: 1, op: "cut", hz: AXIS_DEFAULTS.cut });
    items.push({ beat: totalBeats + 0.5, prio: 1, op: "bdepth", hz: AXIS_DEFAULTS.bdepth });
    items.sort((a, b2) => (a.beat - b2.beat) || (a.prio - b2.prio));
    return { name: "demo-expressive-8bar", bpm: 100, items, totalBeats };
  }

  // ---- 最小SMFパーサ (Type0/1。Note/CC1/CC2/PitchBend/Program/先頭Tempo) ----
  function parseSMF(buf) {
    const dv = new DataView(buf);
    const ascii = (o, n) => String.fromCharCode(...new Uint8Array(buf, o, n));
    if (ascii(0, 4) !== "MThd") throw new Error("MThdなし");
    const ntrk = dv.getUint16(10), div = dv.getUint16(12);
    if (div & 0x8000) throw new Error("SMPTE形式は非対応");
    const tpq = div;
    let pos = 14;
    const tracks = [];
    for (let t = 0; t < ntrk; t++) {
      if (ascii(pos, 4) !== "MTrk") throw new Error("MTrkなし");
      const len = dv.getUint32(pos + 4);
      tracks.push(new Uint8Array(buf, pos + 8, len));
      pos += 8 + len;
    }
    const readVar = (a, p) => {
      let v = 0, i = p;
      for (;;) { const b = a[i++]; v = (v << 7) | (b & 0x7f); if (!(b & 0x80)) break; }
      return [v, i];
    };
    const merged = [];
    for (const a of tracks) {
      let p = 0, tick = 0, run = 0;
      while (p < a.length) {
        let dt; [dt, p] = readVar(a, p); tick += dt;
        let st = a[p];
        if (st < 0x80) { st = run; } else { p++; run = st; }
        if (st === 0xff) { // meta
          const type = a[p++]; let ln; [ln, p] = readVar(a, p);
          if (type === 0x51 && ln === 3) {
            merged.push({ tick, kind: "tempo", mpq: (a[p] << 16) | (a[p + 1] << 8) | a[p + 2] });
          }
          p += ln;
        } else if (st === 0xf0 || st === 0xf7) { let ln; [ln, p] = readVar(a, p); p += ln; }
        else {
          const type = st & 0xf0;
          if (type === 0x90 || type === 0x80 || type === 0xb0 || type === 0xe0 || type === 0xc0) {
            const d1 = a[p++];
            let d2 = 0;
            if (type !== 0xc0) d2 = a[p++];
            merged.push({ tick, kind: "midi", type, d1, d2 });
          } else if (type === 0xa0 || type === 0xd0) { p += (type === 0xa0 ? 2 : 1); }
          else { throw new Error("未対応ステータス " + st.toString(16)); }
        }
      }
    }
    merged.sort((x, y) => x.tick - y.tick);
    const items = [];
    const open = new Map(); // note -> {tick, vel}
    let bpm = 120, lastTick = 0, hasBreathLane = false;
    for (const m of merged) {
      if (m.kind === "tempo" && bpm === 120) bpm = Math.round(60e6 / m.mpq);
      if (m.kind !== "midi") continue;
      const beat = m.tick / tpq;
      if (m.type === 0x90 && m.d2 > 0) open.set(m.d1, { tick: m.tick, vel: m.d2 });
      else if (m.type === 0x80 || (m.type === 0x90 && m.d2 === 0)) {
        const o = open.get(m.d1);
        if (o) {
          open.delete(m.d1);
          items.push({ beat: o.tick / tpq, prio: 5, op: "on", note: m.d1, vel: o.vel, br: 100, dur: Math.max(0.05, (m.tick - o.tick) / tpq), setBreath: false });
        }
      }
      else if (m.type === 0xb0 && (m.d1 === 1 || m.d1 === 2)) {
        if (m.d1 === 2) hasBreathLane = true;
        items.push({ beat, prio: 3, op: "cc", cc: m.d1, val: m.d2 });
      }
      else if (m.type === 0xe0) items.push({ beat, prio: 4, op: "bendRaw", v: m.d1 + m.d2 * 128 });
      else if (m.type === 0xc0) items.push({ beat, prio: 0, op: "preset", n: m.d1 % 4 });
      lastTick = Math.max(lastTick, m.tick);
    }
    const notes = items.filter((it) => it.op === "on");
    if (!notes.length) throw new Error("ノートが見つかりません(形式/チャンネルを確認)");
    if (!hasBreathLane) notes.forEach((n) => { n.setBreath = true; }); // CC2レーンなしSMFの無音防止
    items.sort((a, b2) => (a.beat - b2.beat) || (a.prio - b2.prio));
    return { name: loadedMidiName || "user-midi", bpm, items, totalBeats: lastTick / tpq };
  }

  function beatToCtx(beat) {
    const baseBpm = song.bpm || 120;
    const spb = 60 / (baseBpm * tempoRatio);
    return startCtx + (beat - startBeat) * spb;
  }

  function execItem(MM, it) {
    switch (it.op) {
      case "on": {
        if (it.setBreath !== false && it.br != null) {
          MM._ewi_cc(2, it.br);
          syncBreathUI(it.br);
        }
        MM._ewi_noteOn(it.note, it.vel ?? 100);
        window.EWI.markNote(it.note);
        const durMs = Math.max(60, it.dur * (60 / ((song.bpm || 120) * tempoRatio)) * 1000 * 0.92);
        const nn = it.note;
        setTimeout(() => {
          if (!playing) return;
          const M2 = window.EWI.getMod(); if (!M2) return;
          M2._ewi_noteOff(nn);
        }, durMs);
        break;
      }
      case "cc":
        MM._ewi_cc(it.cc, it.val);
        if (it.cc === 2) syncBreathUI(it.val);
        break;
      case "bend": MM._ewi_pitchbend(stTo14(it.st)); break;
      case "bendRaw": MM._ewi_pitchbend(it.v); break;
      case "cut": MM._ewi_setCutoff(it.hz); break;
      case "bdepth": MM._ewi_setBreathDepth(it.hz); break;
      case "preset":
        MM._ewi_program(it.n);
        { const s = $("selPreset"); if (s) s.value = String(it.n); }
        break;
    }
  }

  function scheduler() {
    if (!playing || !song) return;
    const E = window.EWI, M = E.getMod();
    if (!M) return;
    const ac = EWI_AC(); if (!ac) return;
    const ahead = 0.25;
    const now = ac.currentTime;
    while (song._idx < song.items.length) {
      const it = song.items[song._idx];
      const t = beatToCtx(it.beat);
      if (t > now + ahead) break;
      const delay = Math.max(0, (t - now) * 1000);
      ((ev) => setTimeout(() => {
        if (!playing) return;
        const MM = window.EWI.getMod(); if (!MM) return;
        try { execItem(MM, ev); } catch (err) { console.error("seq exec failed", err); }
      }, delay))(it);
      song._idx++;
    }
    // 終端
    const endT = beatToCtx(song.totalBeats + 1);
    if (now >= endT) {
      if ($("chkLoop").checked) {
        song._idx = 0; startCtx = now + 0.1; startBeat = 0;
        $("seqPos").textContent = "loop";
      } else stop();
    } else {
      const curBeat = startBeat + (now - startCtx) * ((song.bpm || 120) * tempoRatio) / 60;
      $("seqPos").textContent = `beat ${curBeat.toFixed(1)} / ${song.totalBeats.toFixed(1)}`;
    }
  }

  function EWI_AC() { return (window.EWI && window.EWI.getAC && window.EWI.getAC()) || window.__sharedAC || null; }
  function syncBreathUI(v) {
    const r = $("rngBreath"); if (r) { r.value = v; $("valBreath").textContent = v; }
  }

  async function play() {
    const E = window.EWI;
    if (!song) song = buildDemo();
    await E.ensureAudio();
    const ac = (E.getAC && E.getAC()) || window.__sharedAC;
    if (!ac) { $("seqMsg").textContent = "Audio初期化失敗"; return; }
    window.__sharedAC = ac;
    const M = E.getMod(); if (!M) return;
    song._idx = 0;
    tempoRatio = parseFloat($("rngTempo").value) || 1;
    startCtx = window.__sharedAC.currentTime + 0.15;
    startBeat = 0;
    playing = true;
    clearInterval(timer);
    timer = setInterval(scheduler, 25);
    $("btnSeqPlay").textContent = "停止";
    $("seqMsg").textContent = `再生中: ${song.name} (${song.items.filter((i) => i.op === "on").length} notes, bpm ${song.bpm} x${tempoRatio.toFixed(2)})`;
  }
  function stop() {
    playing = false;
    clearInterval(timer);
    const E = window.EWI, M = E && E.getMod();
    if (M) {
      try {
        M._ewi_cc(123, 0); M._ewi_cc(2, 0); M._ewi_cc(1, 0);
        M._ewi_cc(65, 0); M._ewi_pitchbend(8192);
      } catch (_) {}
    }
    E && E.markNote(-1);
    const b = $("btnSeqPlay"); if (b) b.textContent = "シーケンス再生";
    const m = $("seqMsg"); if (m && playing === false && song) m.textContent = `停止: ${song.name}`;
  }

  window.addEventListener("DOMContentLoaded", () => {
    $("btnSeqDemo").onclick = () => {
      stop(); song = buildDemo();
      const n = song.items.filter((i) => i.op === "on").length;
      $("seqMsg").textContent = `セット: ${song.name} (${n} notes, scoop/fall/porta/swell入り)`;
    };
    $("btnSeqPlay").onclick = async () => { if (playing) stop(); else await play(); };
    $("btnSeqStop").onclick = stop;
    $("rngTempo").oninput = (e) => {
      $("valTempo").textContent = parseFloat(e.target.value).toFixed(2) + "x";
      const nr = parseFloat(e.target.value) || 1;
      // 再生中のテンポ変更は現在位置を基準にリベース(飛び防止)
      if (playing && song) {
        const ac = EWI_AC();
        if (ac) startBeat = startBeat + (ac.currentTime - startCtx) * ((song.bpm || 120) * tempoRatio) / 60;
        startCtx = ac ? ac.currentTime : startCtx;
      }
      tempoRatio = nr;
    };
    $("fileMidi").onchange = async (e) => {
      const f = e.target.files[0]; if (!f) return;
      loadedMidiName = f.name;
      try {
        const buf = await f.arrayBuffer();
        const s = parseSMF(buf);
        stop();
        song = { name: s.name, bpm: s.bpm, items: s.items, totalBeats: s.totalBeats };
        const n = s.items.filter((i) => i.op === "on").length;
        $("seqMsg").textContent = `読込: ${s.name} (${n} notes, bpm ${s.bpm})`;
      } catch (err) { $("seqMsg").textContent = "MIDI読込失敗: " + err.message; }
    };
  });
})();
