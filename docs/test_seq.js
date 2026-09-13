// test_seq.js — seq.jsの配線回帰テスト (Node + DOMスタブ)
// 151行目のIIFE引数欠落(全ノート無音)の再発防止。実WASMは使わずModをモックする。
const fs = require('fs');
const path = require('path');
const vm = require('vm');

let failures = [];
// ロード時エラーを飲み込んで無音終了しないよう即時出力する
process.on('uncaughtException', (e) => {
  failures.push('uncaught: ' + (e && e.message));
  console.error('UNCAUGHT: ' + (e && e.stack || e));
});

function makeEl(id) {
  return {
    _id: id, value: id === 'rngTempo' ? '1' : '', textContent: '',
    checked: false, files: [], onclick: null, oninput: null, onchange: null,
  };
}
const els = {};
const listeners = {};
const t0 = Date.now();
const fakeAC = {};
Object.defineProperty(fakeAC, 'currentTime', { get: () => (Date.now() - t0) / 1000 });

const calls = { noteOn: [], noteOff: [], cc: [], bend: [], cut: [], program: [] };
const mockMod = {
  _ewi_cc: (c, v) => calls.cc.push([c, v]),
  _ewi_noteOn: (n, v) => calls.noteOn.push([n, v]),
  _ewi_noteOff: (n) => calls.noteOff.push(n),
  _ewi_pitchbend: (v) => calls.bend.push(v),
  _ewi_setCutoff: (hz) => calls.cut.push(hz),
  _ewi_setBreathDepth: () => {},
  _ewi_program: (p) => calls.program.push(p),
};
const mockEWI = {
  getMod: () => mockMod,
  getAC: () => fakeAC,
  ensureAudio: async () => {},
  markNote: () => {},
};

const sandbox = {
  window: { __sharedAC: fakeAC, EWI: mockEWI },
  console,
  setTimeout, clearTimeout, setInterval, clearInterval,
  parseFloat, Math, Date, Map, Uint8Array, DataView, String, Error,
};
sandbox.window.addEventListener = (t, fn) => { listeners[t] = fn; };
sandbox.document = { getElementById: (id) => (els[id] || (els[id] = makeEl(id))) };
sandbox.globalThis = sandbox;
vm.createContext(sandbox);
try {
  vm.runInContext(fs.readFileSync(path.join(__dirname, 'seq.js'), 'utf8'), sandbox, { filename: 'seq.js' });
} catch (e) {
  console.error('NG: seq.js load failed: ' + (e && e.message));
  process.exit(1);
}
if (typeof listeners['DOMContentLoaded'] !== 'function') {
  console.error('NG: DOMContentLoaded handler not registered');
  process.exit(1);
}

(async () => {
  listeners['DOMContentLoaded']();
  els['btnSeqDemo'].onclick();                       // デモセット
  await els['btnSeqPlay'].onclick();                 // 再生開始
  await new Promise((r) => setTimeout(r, 3400));     // 先頭数音+最初のポルタを待つ
  els['btnSeqStop'].onclick();                       // 停止(タイマ解除)
  await new Promise((r) => setTimeout(r, 300));

  const assert = (c, m) => { if (!c) failures.push(m); };
  assert(calls.noteOn.length >= 2, `noteOnが呼ばれない (${calls.noteOn.length})`);
  assert(calls.cc.some(([c]) => c === 2), 'CC2(Breath)が送られない');
  assert(calls.noteOff.length >= 1, 'noteOffが呼ばれない');
  assert(calls.bend.length >= 1, 'bend(scoop/fall)が送られない');
  assert(calls.cc.some(([c]) => c === 65), 'CC65(ポルタ)が送られない');
  assert(calls.cut.length >= 1, 'cutoff操作が送られない');
  assert(els['seqMsg'].textContent.includes('demo-expressive'), 'seqMsgに曲名が出ない: ' + els['seqMsg'].textContent);
  if (failures.length) { console.error('NG:\n - ' + failures.join('\n - ')); process.exit(1); }
  console.log(JSON.stringify({ noteOn: calls.noteOn.length, noteOff: calls.noteOff.length, cc: calls.cc.length, bend: calls.bend.length, cut: calls.cut.length }));
  console.log('SEQ-OK');
})();
