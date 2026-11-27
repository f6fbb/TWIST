using System.Globalization;
using Twist.App.Lang;

namespace Twist.App.Services;

// Lowers the sample rate by a whole factor, to about 12 kHz (48 and 96
// kHz: 12000 Hz; 44.1 kHz: 11025 Hz; 16 kHz and below: unchanged): the
// beacons are below 2.7 kHz. The capture of libtwist (twaudio.c) already
// keeps the audio at that rate, the one the decoder reads: the recordings
// are written as they are (factor 1); this is for a rate above it.
// Low-pass first (windowed sinc, Hamming, cut at 0.42 of the new rate,
// about 5 kHz), then one sample in Factor kept. Streamed: the history
// is kept from one block to the next, so a recording has no seam.
public sealed class Decimator
{
    public int Factor { get; }
    public int OutRate { get; }
    readonly float[] _h;
    float[] _hist;
    long _n;

    public Decimator(int fs)
    {
        Factor = FactorFor(fs);
        OutRate = fs / Factor;
        if (Factor == 1) { _h = [1f]; _hist = []; return; }
        int taps = (int)Math.Ceiling(41.25 * Factor) | 1;          // transition about 0.08 of the new rate
        double fc = 0.42 / Factor;                                    // cut-off, in cycles per input sample
        _h = new float[taps];
        int m = taps / 2;
        double sum = 0;
        for (int k = 0; k < taps; k++)
        {
            double t = k - m;
            double sinc = t == 0 ? 2 * fc : Math.Sin(2 * Math.PI * fc * t) / (Math.PI * t);
            double w = 0.54 - 0.46 * Math.Cos(2 * Math.PI * k / (taps - 1));
            _h[k] = (float)(sinc * w);
            sum += _h[k];
        }
        for (int k = 0; k < taps; k++) _h[k] = (float)(_h[k] / sum);   // gain 1 at 0 Hz
        _hist = new float[taps - 1];
    }

    // the whole factor: the largest one leaving at least 11025 Hz and
    // dividing fs exactly (a WAV rate is a whole number)
    public static int FactorFor(int fs)
    {
        for (int d = Math.Max(1, fs / 11025); d > 1; d--)
            if (fs % d == 0) return d;
        return 1;
    }

    public float[] Process(ReadOnlySpan<float> x)
    {
        if (Factor == 1) return x.ToArray();
        var buf = new float[_hist.Length + x.Length];
        _hist.CopyTo(buf, 0);
        x.CopyTo(buf.AsSpan(_hist.Length));
        var y = new List<float>(x.Length / Factor + 1);
        int taps = _h.Length;
        for (int i = _hist.Length; i < buf.Length; i++, _n++)
        {
            if (_n % Factor != 0) continue;
            double acc = 0;
            for (int k = 0; k < taps; k++) acc += _h[k] * buf[i - k];
            y.Add((float)acc);
        }
        _hist = buf[^(taps - 1)..];
        return y.ToArray();
    }
}

// A WAV written as the audio comes (the continuous recording): 16-bit
// mono, its header updated every few seconds, so that a file cut short
// (program stopped, power lost) can still be played; the comment (who,
// when, the radio frequency) added at the end.
public sealed class WavStream : IDisposable
{
    readonly FileStream _f;
    readonly string _comment;
    long _samples;
    long _flushedAt;
    public int Rate { get; }
    public string Path { get; }
    public long Samples => _samples;

    public WavStream(string path, int rate, string comment)
    {
        Path = path;
        Rate = rate;
        _comment = comment;
        _f = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.Read);
        Header();
    }

    void Header()
    {
        long data = _samples * 2;
        var h = new byte[44];
        using (var w = new BinaryWriter(new MemoryStream(h)))
        {
            w.Write("RIFF"u8); w.Write((int)Math.Min(uint.MaxValue - 8, 36 + data)); w.Write("WAVE"u8);
            w.Write("fmt "u8); w.Write(16); w.Write((short)1); w.Write((short)1); w.Write(Rate); w.Write(Rate * 2);
            w.Write((short)2); w.Write((short)16);
            w.Write("data"u8); w.Write((int)Math.Min(int.MaxValue, data));
        }
        long pos = _f.Position;
        _f.Position = 0;
        _f.Write(h);
        _f.Position = Math.Max(pos, 44);
    }

    public void Write(ReadOnlySpan<float> x)
    {
        var b = new byte[x.Length * 2];
        for (int i = 0; i < x.Length; i++)
        {
            short v = (short)Math.Clamp(Math.Round(x[i] * 32767.0), -32768, 32767);
            b[2 * i] = (byte)v; b[2 * i + 1] = (byte)(v >> 8);
        }
        _f.Write(b);
        _samples += x.Length;
        if (_samples - _flushedAt >= 5L * Rate)               // every 5 s of audio
        {
            Header();
            _f.Flush();
            _flushedAt = _samples;
        }
    }

    public void Dispose()
    {
        // the comment after the data (LIST/INFO/ICMT, as Wav.WriteMono16)
        var c = System.Text.Encoding.UTF8.GetBytes(_comment + "\0");
        _f.Position = 44 + _samples * 2;
        using (var w = new BinaryWriter(_f, System.Text.Encoding.UTF8, leaveOpen: true))
        {
            w.Write("LIST"u8); w.Write(4 + 8 + c.Length + (c.Length & 1)); w.Write("INFO"u8);
            w.Write("ICMT"u8); w.Write(c.Length); w.Write(c);
            if ((c.Length & 1) != 0) w.Write((byte)0);
        }
        long riff = _f.Position - 8;
        Header();
        _f.Position = 4;
        _f.Write(BitConverter.GetBytes((int)Math.Min(uint.MaxValue, riff)));
        _f.Dispose();
    }
}

// The recordings (Réception › Garder les 2 dernières minutes, Enregistrer
// en continu): in <journal folder>/enregistrements/<day>/, named by the UTC
// time of their first sample and the radio frequency, at the rate of the
// capture (about 12 kHz, 1.4 MB a minute); kept until the user removes
// them (the WAV of the passes, in wav/, are pruned; these are not).
public static class Recordings
{
    static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

    public static string Folder(string logFolder) => System.IO.Path.Combine(logFolder, "enregistrements");

    public static string PathFor(string logFolder, DateTime startUtc, double? radioMHz, string kind)
    {
        var dir = System.IO.Path.Combine(Folder(logFolder), startUtc.ToString("yyyy-MM-dd", Inv));
        Directory.CreateDirectory(dir);
        return System.IO.Path.Combine(dir, $"twist-{startUtc:yyyyMMdd-HHmmss}Z"
            + (radioMHz is double f ? $"-{f.ToString("0.000000", Inv)}MHz" : "") + $"-{kind}.wav");
    }

    public static string Comment(string app, DateTime startUtc, double? radioMHz, int rate, string source) =>
        L.F("{0} · premier échantillon {1}Z · ", app, startUtc.ToString("yyyy-MM-dd HH:mm:ss.f", Inv))
        + (radioMHz is double m ? $"radio {m.ToString("0.000000", Inv)} MHz USB · " : "") + $"{rate} Hz · {source}";

    // the newest audio (the last `seconds`), written at once
    public static string SaveLast(string logFolder, float[] x, int fs, DateTime startUtc, double? radioMHz, string app, string source)
    {
        var d = new Decimator(fs);
        var y = d.Process(x);
        var path = PathFor(logFolder, startUtc, radioMHz, $"{Math.Round(x.Length / (double)fs):0}s");     // "-120s"
        Wav.WriteMono16(path, y, d.OutRate, Comment(app, startUtc, radioMHz, d.OutRate, source));
        return path;
    }
}

// A continuous recording: the audio pulled by the receiver's loop
// (Receiver.StartRecording), written as it comes.
public sealed class Recorder : IDisposable
{
    readonly Decimator _dec;
    readonly WavStream _wav;
    readonly object _lock = new();
    public DateTime StartUtc { get; }
    public string Path => _wav.Path;
    public double Seconds { get { lock (_lock) return (double)_wav.Samples / _wav.Rate; } }
    public long Bytes { get { lock (_lock) return 44 + _wav.Samples * 2; } }
    public int Gaps { get; private set; }

    public Recorder(string logFolder, int fs, DateTime startUtc, double? radioMHz, string app, string source)
    {
        StartUtc = startUtc;
        _dec = new Decimator(fs);
        _wav = new WavStream(Recordings.PathFor(logFolder, startUtc, radioMHz, "continu"), _dec.OutRate,
                             Recordings.Comment(app, startUtc, radioMHz, _dec.OutRate, source));
    }

    public void Write(ReadOnlySpan<float> x)
    {
        var y = _dec.Process(x);
        lock (_lock) _wav.Write(y);
    }

    // samples lost (the sound card restarted, the program too busy)
    public void Gap() => Gaps++;

    public void Dispose()
    {
        lock (_lock) _wav.Dispose();
    }
}
