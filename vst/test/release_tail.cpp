// release_tail.cpp — ブレス110->0ステップ時のテールを10ms刻みで計測。
// 全体RMS / 低域(500Hz LPF)比率 / cutoff / level を出す。44.1/48k両対応。
#include <cstdio>
#include <cmath>
#include <vector>
#include "ewi_synth.h"

static double rms (const float* p, int n)
{
  double s = 0;
  for (int i = 0; i < n; i++) s += (double)p[i] * p[i];
  return sqrt (s / (n ? n : 1));
}

int main ()
{
  static EwiSynth s;
  const float sr = 48000.f;
  const int BLK = 480; // 10ms
  std::vector<float> L (BLK), R (BLK);
  const float gammas[] = {1.0f, 1.5f, 2.0f};
  for (int gi = 0; gi < 3; gi++)
  {
    Ewi_Init (&s, sr);
    Ewi_SetPreset (&s, 0); // Axis
    Ewi_SetFilterGamma (&s, gammas[gi]);
    // FXを切って素のフィルタ+VCAだけ見る
    Ewi_SetDelayMix (&s, 0.f);
    Ewi_SetRevMix (&s, 0.f);
    Ewi_NoteOn (&s, 69, 100);
    Ewi_CC (&s, 2, 110);
    for (int i = 0; i < 50; i++) // 0.5s settle
      Ewi_Render (&s, L.data (), R.data (), BLK);

    Ewi_CC (&s, 2, 0); // ブレス離す (NoteはOnのまま)
    float lp = 0.f;
    const float c = 1.f - expf (-2.f * 3.14159265f * 500.f / sr);
    printf ("== gamma %.1f ==\n", gammas[gi]);
    printf ("t_ms rms_all rms_low low_ratio cutoff\n");
    for (int b = 0; b < 50; b++)
    {
      Ewi_Render (&s, L.data (), R.data (), BLK);
      double eAll = 0, eLow = 0;
      for (int i = 0; i < BLK; i++)
      {
        lp += (L[i] - lp) * c;
        eAll += (double)L[i] * L[i];
        eLow += (double)lp * lp;
      }
      eAll = sqrt (eAll / BLK);
      eLow = sqrt (eLow / BLK);
      int t = (b + 1) * 10;
      if (t == 10 || t == 50 || t == 100 || t == 150 || t == 200 || t == 300 || t == 500)
        printf ("%4d %.5f %.5f %.2f %.0f\n",
                t, eAll, eLow, eAll > 1e-6 ? eLow / eAll : 0, Ewi_GetCutoffHz (&s));
    }
  }
  return 0;
}
