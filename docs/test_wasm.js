// test_wasm.js — Pico版 + PC-HQ版の両WASM発音テスト
async function testOne(path, expectQuality) {
  // キャッシュ回避のため毎回requireキャッシュをクリア
  delete require.cache[require.resolve(path)];
  const EwiModule = require(path);
  const M = await EwiModule();
  const q = M._ewi_quality();
  if (q !== expectQuality) throw new Error(`${path}: quality=${q}, expected ${expectQuality}`);
  M._ewi_init(48000);
  M._ewi_program(0);
  M._ewi_noteOn(69, 100);
  M._ewi_cc(2, 100);
  const N = 4800;
  let sum = 0, peak = 0;
  for (let off = 0; off < N; off += 512) {
    const n = Math.min(512, N - off);
    M._ewi_render(n);
    const pL = M._ewi_ptrL() / 4;
    const a = M.HEAPF32.subarray(pL, pL + n);
    for (let i = 0; i < n; i++) { sum += a[i] * a[i]; peak = Math.max(peak, Math.abs(a[i])); }
  }
  const rms = Math.sqrt(sum / N);
  console.log(JSON.stringify({ mod: path, quality: q, rms: +rms.toFixed(5), peak: +peak.toFixed(5) }));
  if (!(rms > 0.001 && peak < 1.0)) throw new Error(`${path}: no sound or clip`);
  M._ewi_cc(2, 0);
  M._ewi_render(512);
  // 空間系: ディレイ+リバーブを効かせて再発音
  M._ewi_setDlyMix(0.4); M._ewi_setDlyTime(0.2); M._ewi_setDlyFb(0.5);
  M._ewi_setRevMix(0.4); M._ewi_setRevSize(0.8);
  M._ewi_noteOn(69, 100);
  M._ewi_cc(2, 110);
  let wsum = 0, wpeak = 0;
  const W = 9600;
  for (let off = 0; off < W; off += 512) {
    const n = Math.min(512, W - off);
    M._ewi_render(n);
    const pL = M._ewi_ptrL() / 4;
    const a = M.HEAPF32.subarray(pL, pL + n);
    for (let i = 0; i < n; i++) { wsum += a[i] * a[i]; wpeak = Math.max(wpeak, Math.abs(a[i])); }
  }
  const wrms = Math.sqrt(wsum / W);
  console.log(JSON.stringify({ mod: path, wetRms: +wrms.toFixed(5), wetPeak: +wpeak.toFixed(5) }));
  if (!(wrms > 0.001 && wpeak < 1.0)) throw new Error(`${path}: wet no sound or clip`);
  // リリース後も残響テールが出ること
  M._ewi_cc(2, 0);
  M._ewi_render(512);
  const pL2 = M._ewi_ptrL() / 4;
  const t = M.HEAPF32.subarray(pL2, pL2 + 512);
  let ts = 0; for (let i = 0; i < 512; i++) ts += t[i] * t[i];
  console.log(JSON.stringify({ mod: path, tailRms: +Math.sqrt(ts / 512).toFixed(5) }));
  return { rms, peak };
}

(async () => {
  const pico = await testOne('./ewi_synth.js', 0);
  const hq = await testOne('./ewi_synth_hq.js', 1);
  // 同一フレーズで極端に乖離していないことだけ確認 (同一音色の別実装のため完全一致は求めない)
  if (Math.abs(pico.rms - hq.rms) > 0.1) throw new Error('Pico/HQ rms diverged too much');
  console.log('OK (Pico + PC-HQ)');
})().catch((e) => { console.error('NG: ' + e.message); process.exit(1); });
