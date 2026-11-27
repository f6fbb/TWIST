using Twist.App.Services;
using Twist.Native;
using Xunit;

namespace Twist.Tests;

public class ReceiverTests
{
    // Replays a file through the receiver as fast as possible and
    // collects what it reports.
    static List<Spot> Replay(string path)
    {
        var spots = new List<Spot>();
        using var rx = new Receiver();
        rx.SpotDecoded += s => { lock (spots) spots.Add(s); };
        rx.StartFile(path, fast: true);
        var until = DateTime.UtcNow.AddMinutes(5);
        while (rx.Running && DateTime.UtcNow < until) Thread.Sleep(50);
        return spots;
    }

    // the keying detector of each pass sees the real frame while it is
    // there, at its frequency, and nothing else in the zone
    [Fact]
    public void Real_recording_frame_in_progress_seen()
    {
        var hits = new List<TwlKeyed>();
        using var rx = new Receiver();
        rx.KeyedSeen += (k, _) => { lock (hits) hits.AddRange(k); };
        rx.StartFile("f5zzo.wav", fast: true);
        var until = DateTime.UtcNow.AddMinutes(5);
        while (rx.Running && DateTime.UtcNow < until) Thread.Sleep(50);
        Assert.Contains(hits, h => Math.Abs(h.Freq - 1564) < 8);
        Assert.All(hits, h => Assert.InRange(h.Freq, 1550, 1580));
    }

    // the progress of a replay: up to the length of the file, finished
    // at its end; stopped on the way, not finished
    [Fact]
    public void Replay_progress_to_the_end()
    {
        using var rx = new Receiver();
        rx.StartFile("f5zzo.wav", fast: true);
        Assert.True(rx.Replaying);
        Assert.InRange(rx.FileSeconds, 30, 600);
        var until = DateTime.UtcNow.AddMinutes(5);
        while (rx.Running && DateTime.UtcNow < until) Thread.Sleep(20);
        Assert.False(rx.Replaying);
        Assert.True(rx.FileFinished);
        Assert.Equal(rx.FileSeconds, rx.FilePosition, 3);

        // (bounds wide enough for a busy test machine)
        rx.StartFile("f5zzo.wav", fast: false);           // real speed: about 1 s of audio in 1 s
        Thread.Sleep(1500);
        Assert.InRange(rx.FilePosition, 0.5, 3.5);
        double p0 = rx.FilePosition;
        var sw = System.Diagnostics.Stopwatch.StartNew();
        rx.Fast = true;                                   // switched while it plays
        while (rx.FilePosition < p0 + 9 && sw.Elapsed.TotalSeconds < 30) Thread.Sleep(5);
        rx.Fast = false;
        Assert.True(sw.Elapsed.TotalSeconds < 6, $"9 s of audio in {sw.Elapsed.TotalSeconds:0.0} s");
        Assert.True(rx.Running);                          // (the file is longer than that)
        double p1 = rx.FilePosition;
        Thread.Sleep(1500);
        Assert.InRange(rx.FilePosition - p1, 0.3, 4);     // back to real speed
        rx.Stop();
        Assert.False(rx.FileFinished);
    }

    // PI4, when asked: the real recording of F5ZZO, its callsign, then the
    // "/JN12AX" of the next minute (a spot without callsign, its locator)
    [Fact]
    public void Real_recording_pi4_when_asked()
    {
        var spots = new List<Spot>();
        using var rx = new Receiver();
        var s = rx.Settings;
        Assert.False(s.Protocols.HasFlag(TwlProto.Pi4));          // not by default
        s.Protocols |= TwlProto.Pi4;
        rx.Settings = s;
        rx.SpotDecoded += x => { lock (spots) spots.Add(x); };
        rx.StartFile("pi4_f5zzo.wav", fast: true);
        var until = DateTime.UtcNow.AddMinutes(5);
        while (rx.Running && DateTime.UtcNow < until) Thread.Sleep(50);
        var pi4 = spots.Where(x => x.Proto == TwlProto.Pi4).OrderBy(x => x.StartUtc).ToList();
        Assert.Equal(2, pi4.Count);
        Assert.Equal(("F5ZZO", ""), (pi4[0].Call, pi4[0].Loc));
        Assert.Equal(("", "JN12AX"), (pi4[1].Call, pi4[1].Loc));
        Assert.InRange((pi4[1].StartUtc - pi4[0].StartUtc).TotalSeconds, 59.5, 60.5);
        Assert.All(pi4, x => Assert.InRange(x.FreqHz, 795, 802));
        Assert.All(pi4, x => Assert.InRange(x.Snr, -13, -9));
    }

    // the band of the waterfall: around the OOK zone, the PI4 zone, or both
    [Fact]
    public void Waterfall_band_around_the_zones()
    {
        Assert.Equal((1000.0, 2000.0), Twist.App.Controls.WaterfallView.Range(0, 0, 1500, 1000, true, 200, false, 800, 150));
        Assert.Equal((550.0, 1800.0), Twist.App.Controls.WaterfallView.Range(0, 0, 1500, 1000, true, 200, true, 800, 150));
        Assert.Equal((300.0, 1300.0), Twist.App.Controls.WaterfallView.Range(0, 0, 1500, 1000, false, 200, true, 800, 150));
        // the fixed bands
        Assert.Equal((300.0, 2700.0), Twist.App.Controls.WaterfallView.Range(300, 2700, 1500, 1000, true, 200, true, 800, 150));
        Assert.Equal((200.0, 2000.0), Twist.App.Controls.WaterfallView.Range(200, 2000, 1500, 1000, true, 200, true, 800, 150));
    }

    [Fact]
    public void Real_recording_classic_frame_once()
    {
        var spots = Replay("f5zzo.wav");
        var s = Assert.Single(spots);
        Assert.Equal(TwlProto.Classic, s.Proto);
        Assert.Equal("F5ZZO", s.Call);
        Assert.InRange(s.FreqHz, 1560, 1568);
    }

    // A beacon sequence rendered by the transmitter (TWIST L, pause,
    // TWIST S, pause, classic frame) at 48 kHz in noise: each frame is
    // reported exactly once, with the right spacing between starts.
    [Fact]
    public void Generated_sequence_each_frame_once()
    {
        LibTwist.TxSettingsDefault(out var ts);
        ts.Fs = 48000;
        using var tx = new TwistTransmitter();
        tx.Start(ts,
            TwlStep.Pause(2),
            TwlStep.Frame(TwlProto.TwistL, "F1HDI", "JN18CS"),
            TwlStep.Pause(4),
            TwlStep.Frame(TwlProto.TwistS, "F6FBB"),
            TwlStep.Pause(4),
            TwlStep.Frame(TwlProto.Classic, "F5ZZO"),
            TwlStep.Pause(2));
        var x = new List<float>();
        var buf = new float[4800];
        int m;
        while ((m = tx.Render(buf)) > 0) x.AddRange(buf.Take(m));
        var rnd = new Random(1);
        // SNR about -12 dB in 2500 Hz while keyed on
        double sigma = Math.Sqrt(ts.Level * ts.Level / 2 / (Math.Pow(10, -1.2) * 2500.0 / (ts.Fs / 2.0)));
        var y = x.Select(v => (float)(v + sigma * Gauss(rnd))).ToArray();
        float peak = y.Max(Math.Abs);
        var path = Path.Combine(Path.GetTempPath(), "twist_rx_test.wav");
        WriteWav(path, y.Select(v => v / peak * 0.9f).ToArray(), ts.Fs);

        var spots = Replay(path).OrderBy(s => s.StartUtc).ToList();
        Assert.Equal(3, spots.Count);
        Assert.Equal((TwlProto.TwistL, "F1HDI", "JN18CS"), (spots[0].Proto, spots[0].Call, spots[0].Loc));
        Assert.Equal((TwlProto.TwistS, "F6FBB"), (spots[1].Proto, spots[1].Call));
        Assert.Equal((TwlProto.Classic, "F5ZZO"), (spots[2].Proto, spots[2].Call));
        // starts: 2 s, 2 + 41.088 + 4, + 30.592 + 4
        Assert.InRange((spots[1].StartUtc - spots[0].StartUtc).TotalSeconds, 45.0, 45.2);
        Assert.InRange((spots[2].StartUtc - spots[1].StartUtc).TotalSeconds, 34.5, 34.7);
        Assert.All(spots, s => Assert.InRange(s.FreqHz, 1499, 1501));
    }

    [Fact]
    public void Maidenhead_distance_azimuth()
    {
        var r = Maidenhead.DistanceAzimuth("JN12AX", "JN18CS");
        Assert.NotNull(r);
        Assert.InRange(r!.Value.km, 630, 660);
        Assert.InRange(r.Value.az, 0, 10);
        Assert.Null(Maidenhead.DistanceAzimuth("JN12AX", "ZZ99"));
    }

    static double Gauss(Random r) =>
        Math.Sqrt(-2 * Math.Log(1 - r.NextDouble())) * Math.Cos(2 * Math.PI * r.NextDouble());

    static void WriteWav(string path, float[] x, int fs)
    {
        using var w = new BinaryWriter(File.Create(path));
        w.Write("RIFF"u8); w.Write(36 + 2 * x.Length); w.Write("WAVEfmt "u8);
        w.Write(16); w.Write((short)1); w.Write((short)1); w.Write(fs); w.Write(2 * fs);
        w.Write((short)2); w.Write((short)16); w.Write("data"u8); w.Write(2 * x.Length);
        foreach (var v in x) w.Write((short)Math.Round(Math.Clamp(v, -1f, 1f) * 32767f));
    }
}

// Live loopback through a real sound device: one TWIST S frame played on
// the default output while the receiver captures the default input.
// Needs a loopback (virtual cable, PulseAudio null-sink monitor...), so
// it only runs with TWIST_LOOPBACK=1.
[Collection("sound card")]
public class LoopbackTests
{
    [Fact]
    public void Live_loopback_frame_decoded()
    {
        if (Environment.GetEnvironmentVariable("TWIST_LOOPBACK") != "1") return;
        var spots = new List<Spot>();
        using var rx = new Receiver();
        rx.SpotDecoded += s => { lock (spots) spots.Add(s); };
        rx.StartDevice(LibTwist.AudioDefault, "default");

        var devs = TwistAudio.Devices();
        LibTwist.TxSettingsDefault(out var ts);
        ts.Fs = (int)devs.First(d => d.IsDefaultOut != 0).DefaultRate;
        using var tx = new TwistTransmitter();
        tx.Start(ts, TwlStep.Pause(1), TwlStep.Frame(TwlProto.TwistS, "F6FBB"), TwlStep.Pause(1));
        using var out_ = new TwistAudio();
        out_.StartOutput(tx);
        var until = DateTime.UtcNow.AddSeconds(50);
        while (DateTime.UtcNow < until) { lock (spots) if (spots.Count > 0) break; Thread.Sleep(200); }
        var s = Assert.Single(spots);
        Assert.Equal((TwlProto.TwistS, "F6FBB"), (s.Proto, s.Call));
        Assert.Equal(0, rx.Stats.Overruns);
    }
}
