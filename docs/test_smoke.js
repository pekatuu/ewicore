// test_smoke.js — app.jsのブラウザなしスモークテスト
// pump()のReferenceError(LOOKAHEAD/BLOCK欠落)の再発防止。AudioContext/DOMをスタブ化し、
// startAudio→pumpまで実際に駆動する。
const fs = require('fs');
const path = require('path');
const vm = require('vm');

function makeEl(id) {
  return {
    _id: id, value: '0', textContent: '', checked: false, files: [],
    options: [], onclick: null, oninput: null, onchange: null,
    innerHTML: '', appendChild() {},
  };
}
const els = {};
const listeners = {};
const acInstances = [];

class FakeAC {
  constructor() {
    this.sampleRate = 48000;
    this.currentTime = 0;
    this.destination = {};
    acInstances.push(this);
  }
  async resume() {}
  createBuffer(ch, n) { return { getChannelData: () => new Float32Array(n) }; }
  createBufferSource() { return { connect() {}, start() {} }; }
}

const modCalls = { render: 0, noteOn: [], midi: [], cc: [] };
const modStub = {
  _ewi_init() {}, _ewi_program() {}, _ewi_setCutoff() {}, _ewi_setBreathDepth() {},
  _ewi_setFilterGamma() {},
  _ewi_setReso() {}, _ewi_setFormant() {}, _ewi_setGlide() {},
  _ewi_cc: (c, v) => modCalls.cc.push([c, v]),
  _ewi_noteOff() {},
  _ewi_midi: (st, d1, d2) => modCalls.midi.push([st, d1, d2]),
  _ewi_noteOn: (n) => modCalls.noteOn.push(n),
  _ewi_setDlyMix() {}, _ewi_setDlyTime() {}, _ewi_setDlyFb() {},
  _ewi_setRevMix() {}, _ewi_setRevSize() {},
  _ewi_render() { modCalls.render++; },
  _ewi_ptrL: () => 0, _ewi_ptrR: () => 0, _ewi_quality: () => 0,
  HEAPF32: new Float32Array(4096),
};

const sandbox = {
  window: { AudioContext: FakeAC },
  document: {
    getElementById: (id) => (els[id] || (els[id] = makeEl(id))),
    createElement: () => ({ textContent: '', appendChild() {} }),
  },
  requestAnimationFrame: () => 0,
  setInterval: () => 1, clearInterval: () => {},
  setTimeout, clearTimeout,
  console, Float32Array, Math,
  EwiModule: async () => modStub,
};
sandbox.window.addEventListener = (t, fn) => { listeners[t] = fn; };
sandbox.globalThis = sandbox;
vm.createContext(sandbox);

(async () => {
  const failures = [];
  const assert = (c, m) => { if (!c) failures.push(m); };
  let src;
  try {
    src = fs.readFileSync(path.join(__dirname, 'app.js'), 'utf8');
    vm.runInContext(src, sandbox, { filename: 'app.js' });
  } catch (e) {
    console.error('NG: app.js load failed: ' + (e && e.message));
    process.exit(1);
  }
  // 定数チェック (静的)
  assert(/const\s+BLOCK\s*=\s*\d+/.test(src), 'BLOCK定数がない');
  assert(/LOOKAHEAD\s*=\s*[\d.]+/.test(src), 'LOOKAHEAD定数がない');
  // 実行チェック (テスト音ボタン経路: startAudio→noteOn→pump)
  listeners['DOMContentLoaded']();
  await els['btnSelf'].onclick();
  assert(modCalls.noteOn.includes(69), 'selftestのnoteOn(69)が呼ばれない');
  // 空間系スライダ配線 (例外なく通ること)
  for (const id of ['pDlyMix', 'pDlyTime', 'pDlyFb', 'pRevMix', 'pRevSize', 'pFGamma']) {
    els[id].value = '20';
    els[id].oninput({ target: els[id] });
  }
  assert(acInstances.length === 1, 'AudioContextが作られない');
  acInstances[0].currentTime = 2.0; // 先読み window を満たす
  try {
    vm.runInContext('pump()', sandbox);
  } catch (e) {
    failures.push('pump() throws: ' + (e && e.message));
  }
  assert(modCalls.render >= 1, `wasm renderが呼ばれない (${modCalls.render})`);
  const nextTime = vm.runInContext('nextTime', sandbox);
  assert(nextTime > 0, 'nextTimeが進まない');
  // 外部MIDI回帰: ch2機器のNote/CCがch1正規化でコアに届くこと + ブレス救済
  const lastMidi = () => modCalls.midi[modCalls.midi.length - 1];
  const lastCc = () => modCalls.cc[modCalls.cc.length - 1];
  vm.runInContext('breath = 0; curNote = -1;', sandbox);
  modCalls.midi.length = 0; modCalls.cc.length = 0;
  vm.runInContext('onMidi({data:[0x91,0x40,0x5e]})', sandbox); // 報告ログの1行目相当
  assert(JSON.stringify(lastMidi()) === JSON.stringify([0x90, 0x40, 0x5e]),
    `ch2 NoteOnが正規化されない: ${JSON.stringify(lastMidi())}`);
  assert(JSON.stringify(lastCc()) === JSON.stringify([2, 0x5e]),
    `鍵盤系のベロシティ→ブレス救済が働かない: ${JSON.stringify(lastCc())}`);
  assert(vm.runInContext('breath', sandbox) === 0x5e, 'breath表示が更新されない');
  assert(vm.runInContext('curNote', sandbox) === 0x40, 'curNoteが更新されない');
  vm.runInContext('onMidi({data:[0x81,0x40,0x00]})', sandbox);
  assert(JSON.stringify(lastMidi()) === JSON.stringify([0x80, 0x40, 0x00]),
    `ch2 NoteOffが正規化されない: ${JSON.stringify(lastMidi())}`);
  assert(vm.runInContext('curNote', sandbox) === -1, 'NoteOffでcurNoteが戻らない');
  // EWI式 (CC2先行) ではベロシティでブレスを上書きしない
  const nCc = modCalls.cc.length;
  vm.runInContext('onMidi({data:[0xB1,0x02,0x64]})', sandbox);
  assert(JSON.stringify(lastMidi()) === JSON.stringify([0xB0, 0x02, 0x64]),
    `ch2 CC2が正規化されない: ${JSON.stringify(lastMidi())}`);
  vm.runInContext('onMidi({data:[0x91,0x3b,0x58]})', sandbox);
  assert(JSON.stringify(lastMidi()) === JSON.stringify([0x90, 0x3b, 0x58]),
    `EWI式NoteOnが正規化されない: ${JSON.stringify(lastMidi())}`);
  assert(modCalls.cc.length === nCc, 'EWI吹奏中にブレスが上書きされた');
  assert(vm.runInContext('breath', sandbox) === 0x64, 'EWI式でbreathが保持されない');
  // チャンネルプレッシャー (データ1バイト) とプログラムチェンジ表示
  vm.runInContext('onMidi({data:[0xD1,0x50]})', sandbox);
  assert(JSON.stringify(lastMidi()) === JSON.stringify([0xD0, 0x50, 0]),
    `pressureが正規化されない: ${JSON.stringify(lastMidi())}`);
  assert(vm.runInContext('breath', sandbox) === 0x50, 'pressureがbreath表示に反映されない');
  vm.runInContext('onMidi({data:[0xC1,0x02]})', sandbox);
  assert(JSON.stringify(lastMidi()) === JSON.stringify([0xC0, 0x02, 0]),
    `PCが正規化されない: ${JSON.stringify(lastMidi())}`);
  assert(els['selPreset'].value === '2', `PC表示がNaN/不正: ${els['selPreset'].value}`);
  if (failures.length) { console.error('NG:\n - ' + failures.join('\n - ')); process.exit(1); }
  console.log(JSON.stringify({ render: modCalls.render, nextTime: +nextTime.toFixed(3) }));
  console.log('SMOKE-OK');
})().catch((e) => { console.error('NG: ' + (e && e.stack || e)); process.exit(1); });
