namespace Twist.App.Services;

// "PI4 frame in progress": the PI4 counterpart of twl_keyed, which knows
// only the OOK frames (a PI4 transmission was seen there by its highest
// tone alone, which can fall in the OOK zone). PI4 sends one of 4 tones,
// 234.375 Hz apart, every 1/6 s: averaged over the last seconds, the four
// tones all stand out of the noise. A carrier, a CW identification or an
// OOK frame lights one of them only.
//
// The spectrum of the window (Hann, segments of about one symbol, half
// overlapped) is compared with its median; a carrier frequency c (OZ2M
// convention: tone 0 + 117.19 Hz) of the zone is a frame in progress when
// each of its four tones is at least MinRatio above that median.
public static class Pi4Activity
{
    public const double Spacing = 234.375, CarrierOffset = 117.1875;
    public const double MinRatio = 1.8;

    // the carriers of the PI4 frames present in the last windowS seconds
    // of samples[0..n), strongest first
    public static List<double> Find(float[] samples, int n, int fs, double center, double tol, double windowS = 8)
    {
        var found = new List<double>();
        int len = 1;
        while (len < fs / 6.0) len <<= 1;            // 2048 at 12 kHz: bins of 5.9 Hz
        int span = (int)Math.Min(n, windowS * fs);
        if (fs <= 0 || span < 4 * len) return found;
        double binHz = (double)fs / len;
        double fLo = center - tol - CarrierOffset - 30, fHi = center + tol - CarrierOffset + 3 * Spacing + 30;
        int b0 = Math.Max(1, (int)(fLo / binHz)), b1 = Math.Min(len / 2 - 2, (int)Math.Ceiling(fHi / binHz));
        if (b1 - b0 < 16) return found;

        var win = new double[len];
        for (int i = 0; i < len; i++) win[i] = 0.5 - 0.5 * Math.Cos(2 * Math.PI * i / (len - 1));
        var re = new double[len];
        var im = new double[len];
        var p = new double[b1 + 2];
        int segs = 0;
        for (int start = n - span; start + len <= n; start += len / 2, segs++)
        {
            for (int i = 0; i < len; i++) { re[i] = samples[start + i] * win[i]; im[i] = 0; }
            Dsp.Fft(re, im);
            for (int b = b0; b <= b1 + 1; b++) p[b] += re[b] * re[b] + im[b] * im[b];
        }
        var band = p[b0..(b1 + 1)];
        Array.Sort(band);
        double med = band[band.Length / 2];
        if (med <= 0) return found;

        // power of a tone: the strongest bin within half a bin of it
        double Tone(double f)
        {
            int b = (int)Math.Round(f / binHz);
            double m = p[b];
            if (Math.Abs(f - (b - 1) * binHz) < binHz) m = Math.Max(m, p[b - 1]);
            if (Math.Abs(f - (b + 1) * binHz) < binHz) m = Math.Max(m, p[b + 1]);
            return m / med;
        }
        var cands = new List<(double c, double score)>();
        for (double c = center - tol; c <= center + tol; c += binHz / 2)
        {
            double t0 = c - CarrierOffset, score = double.MaxValue;
            for (int k = 0; k < 4; k++) score = Math.Min(score, Tone(t0 + k * Spacing));
            if (score >= MinRatio) cands.Add((c, score));
        }
        // one per frame: the best carrier, nothing else within 20 Hz of it
        foreach (var (c, _) in cands.OrderByDescending(x => x.score))
            if (found.All(f => Math.Abs(f - c) > 20)) found.Add(c);
        return found;
    }
}
