using Twist.App.Services;
using Twist.Native;
using Xunit;

namespace Twist.Tests;

// The recordings (Services/Recorder.cs): the rate lowered to about 12 kHz
// without aliasing, the WAV written as the audio comes, readable even cut
// short, and a continuous recording without a sample missed or twice.
public class RecordingTests
{
    [Theory]
    [InlineData(48000, 4, 12000)]
    [InlineData(96000, 8, 12000)]
    [InlineData(44100, 4, 11025)]
    [InlineData(192000, 16, 12000)]
    [InlineData(22050, 2, 11025)]
    [InlineData(12000, 1, 12000)]
    [InlineData(8000, 1, 8000)]
    public void Rate_lowered_by_a_whole_factor(int fs, int factor, int outRate)
    {
        var d = new Decimator(fs);
        Assert.Equal(factor, d.Factor);
        Assert.Equal(outRate, d.OutRate);
    }

    static float[] Tone(double f, int fs, int n) =>
        Enumerable.Range(0, n).Select(i => (float)(0.5 * Math.Sin(2 * Math.PI * f * i / fs))).ToArray();

    static double Rms(float[] y, int skip) => Math.Sqrt(y.Skip(skip).Take(y.Length - 2 * skip).Average(v => (double)v * v));

    [Fact]
    public void Band_kept_alias_removed_and_no_seam()
    {
        // 1500 Hz kept (0.354 RMS), 9 kHz (would fold to 3 kHz) removed
        var keep = new Decimator(48000).Process(Tone(1500, 48000, 48000));
        var gone = new Decimator(48000).Process(Tone(9000, 48000, 48000));
        Assert.InRange(Rms(keep, 200), 0.35, 0.36);
        Assert.True(20 * Math.Log10(Rms(gone, 200) / 0.354) < -40, "alias not removed");
        // in blocks of any size: the same as in one go
        var x = Tone(1234, 48000, 30000);
        var whole = new Decimator(48000).Process(x);
        var d = new Decimator(48000);
        var parts = new List<float>();
        for (int i = 0; i < x.Length; i += 777) parts.AddRange(d.Process(x.AsSpan(i, Math.Min(777, x.Length - i))));
        Assert.Equal(whole.Length, parts.Count);
        Assert.True(whole.Zip(parts).Max(p => Math.Abs(p.First - p.Second)) < 1e-6);
    }

    [Fact]
    public void Wav_written_as_it_comes_readable_cut_short_and_closed()
    {
        var path = Path.Combine(Path.GetTempPath(), $"twist-rec-{Guid.NewGuid():N}.wav");
        var x = Tone(800, 12000, 12000 * 7);
        var w = new WavStream(path, 12000, "TWIST Console test");
        for (int i = 0; i < x.Length; i += 1200) w.Write(x.AsSpan(i, 1200));
        // not closed yet (as after a crash): its header says what was flushed
        using (var copy = File.Open(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
        {
            var tmp = path + ".copy.wav";
            using (var o = File.Create(tmp)) copy.CopyTo(o);
            var (cut, fs0) = Wav.ReadMono(tmp);
            Assert.Equal(12000, fs0);
            Assert.InRange(cut.Length, 12000 * 5, x.Length);
            File.Delete(tmp);
        }
        w.Dispose();
        var (y, fs) = Wav.ReadMono(path);
        Assert.Equal(12000, fs);
        Assert.Equal(x.Length, y.Length);
        Assert.True(x.Zip(y).Max(p => Math.Abs(p.First - p.Second)) < 2.0 / 32767);
        Assert.Contains("TWIST Console test", System.Text.Encoding.UTF8.GetString(File.ReadAllBytes(path)));
        File.Delete(path);
    }

    // a 48 kHz frame replayed (the capture lowers it to 12 kHz), recorded
    // from just after its start: the file holds every sample the capture
    // got from then on, once (its length is exactly their count), and the
    // frame decodes from it
    [Fact]
    public void Continuous_recording_without_gap_and_decodable()
    {
        var dir = Path.Combine(Path.GetTempPath(), $"twist-rec-{Guid.NewGuid():N}");
        var src = Path.Combine(dir, "src.wav");
        Directory.CreateDirectory(dir);
        var req = new TestWav.Request(TwlProto.TwistS, "F6FBB", "", 1500, -12, 2, 4, 48000, Seed: 5);
        Wav.WriteMono16(src, TestWav.Make(req), 48000);

        using var rx = new Receiver();
        rx.StartFile(src, fast: true);
        Assert.Equal(12000, rx.Stats.Rate);
        var rec = new Recorder(dir, rx.Stats.Rate, DateTime.UtcNow, 144.489, "TWIST Console test", "src.wav");
        long from = rx.StartRecording(rec);
        var until = DateTime.UtcNow.AddMinutes(3);
        while (rx.Running && DateTime.UtcNow < until) Thread.Sleep(20);
        long to = rx.CapturedSamples;
        Assert.Same(rec, rx.StopRecording());
        rec.Dispose();
        Assert.Equal(0, rec.Gaps);

        var (y, fs) = Wav.ReadMono(rec.Path);
        Assert.Equal(12000, fs);
        Assert.Equal(to - from, y.Length);                       // every sample, once
        Assert.True(y.Length > 30 * 12000);
        var spots = TestWav.Check(y, req with { Rate = fs });
        Assert.Contains(spots, s => s.Proto == TwlProto.TwistS && s.Call == "F6FBB");
        Directory.Delete(dir, true);
    }

    static double y0((float[] x, int fs, DateTime t0) a) => a.x.Length / (double)a.fs;

    // Garder les 2 dernières minutes: what the capture holds, at its rate,
    // named by the time of its first sample; the frame decodes from it
    [Fact]
    public void Last_minutes_kept()
    {
        var dir = Path.Combine(Path.GetTempPath(), $"twist-rec-{Guid.NewGuid():N}");
        var src = Path.Combine(dir, "src.wav");
        Directory.CreateDirectory(dir);
        var req = new TestWav.Request(TwlProto.TwistL, "F6FBB", "JN12AX", 1520, -12, 2, 4, 44100, Seed: 9);
        Wav.WriteMono16(src, TestWav.Make(req), 44100);
        using var rx = new Receiver();
        rx.StartFile(src, fast: true);
        var until = DateTime.UtcNow.AddMinutes(3);
        while (rx.Running && DateTime.UtcNow < until) Thread.Sleep(20);
        var a = rx.TakeLast(120);
        Assert.NotNull(a);
        Assert.Equal(11025, a!.Value.fs);
        Assert.Equal(rx.CapturedSamples, a.Value.x.Length);      // less than 2 minutes captured: all of it
        var t0 = new DateTime(2026, 11, 11, 14, 32, 5, DateTimeKind.Utc);
        var path = Recordings.SaveLast(dir, a.Value.x, a.Value.fs, t0, 432.4, "TWIST Console test", "src.wav");
        Assert.EndsWith(Path.Combine("2026-11-11", $"twist-20261111-143205Z-432.400000MHz-{Math.Round(y0(a.Value)):0}s.wav"), path);
        var (y, fs) = Wav.ReadMono(path);
        Assert.Equal(11025, fs);
        Assert.Equal(a.Value.x.Length, y.Length);
        var spots = TestWav.Check(y, req with { Rate = fs });
        Assert.Contains(spots, s => s.Proto == TwlProto.TwistL && s.Call == "F6FBB" && s.Loc == "JN12AX");
        Directory.Delete(dir, true);
    }
}
