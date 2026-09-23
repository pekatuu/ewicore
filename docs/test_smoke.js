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

const modCalls = { render: 0, noteOn: [] };
const modStub = {
  _ewi_init() {}, _ewi_program() {}, _ewi_setCutoff() {}, _ewi_setBreathDepth() {},
  _ewi_setFilterGamma() {},
  _ewi_setReso() {}, _ewi_setFormant() {}, _ewi_setGlide() {},
  _ewi_cc() {}, _ewi_noteOff() {},
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
  if (failures.length) { console.error('NG:\n - ' + failures.join('\n - ')); process.exit(1); }
  console.log(JSON.stringify({ render: modCalls.render, nextTime: +nextTime.toFixed(3) }));
  console.log('SMOKE-OK');
})().catch((e) => { console.error('NG: ' + (e && e.stack || e)); process.exit(1); });
