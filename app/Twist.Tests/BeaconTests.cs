using Twist.App.Services;
using Twist.Native;
using Xunit;

namespace Twist.Tests;

public class BeaconTests
{
    static DateTime U(int h, int m, int s, int ms = 0) => new(2026, 10, 28, h, m, s, ms, DateTimeKind.Utc);

    [Fact]
    public void Next_start_of_the_cycle()
    {
        Assert.Equal(U(9, 45, 0), Beacon.NextStart(U(9, 37, 12), 15, 0));
        Assert.Equal(U(9, 45, 0), Beacon.NextStart(U(9, 44, 59, 500), 15, 0));
        Assert.Equal(U(10, 0, 0), Beacon.NextStart(U(9, 45, 0), 15, 0));      // strictly after
        Assert.Equal(U(9, 38, 30), Beacon.NextStart(U(9, 37, 31), 1, 30));
        Assert.Equal(U(9, 37, 30), Beacon.NextStart(U(9, 37, 29), 1, 30));
        Assert.Equal(new DateTime(2026, 10, 29, 0, 0, 5, DateTimeKind.Utc), Beacon.NextStart(U(23, 50, 0), 15, 5));
    }

    [Fact]
    public void Durations_and_longest_ptt()
    {
        var l = TwlStep.Frame(TwlProto.TwistL, "F6FBB", "JN12AX");
        double fl = 321 * 0.128;
        Assert.Equal(fl, Beacon.StepSeconds(l), 6);
        var seq = new[] { l, TwlStep.Carrier(10), TwlStep.Pause(5, holdPtt: true), TwlStep.Frame(TwlProto.Classic, "F6FBB") };
        double fc = LibTwist.FrameChips(TwlProto.Classic) * 0.128;
        Assert.Equal(fl + 10 + 5 + fc, Beacon.TotalSeconds(seq), 6);
        Assert.Equal(fl + 10 + 5 + fc, Beacon.LongestPtt(seq), 6);        // the pause holds the PTT
        seq[2] = TwlStep.Pause(5, holdPtt: false);
        Assert.Equal(fl + 10, Beacon.LongestPtt(seq), 6);                 // released: two runs
    }

    [Fact]
    public void Sequence_longer_than_the_watchdog_refused()
    {
        using var b = new Beacon();
        var e = Assert.Throws<ArgumentException>(() =>
            b.Start([TwlStep.Carrier(30)], new TxOutput(LibTwist.AudioDefault, 48000, 1500, 0.5, 20), "test"));
        Assert.Contains("chien de garde", e.Message);
        Assert.False(b.Active);
    }
}

// Through a loopback sound device (output looped to the default input),
// TWIST_LOOPBACK=1: a sequence scheduled at a given second is decoded by
// the receiver with its start on that second.
[Collection("sound card")]          // one test at a time on the loopback
public class BeaconLoopbackTests
{
    [Fact]
    public void Scheduled_sequence_starts_on_its_second_and_decodes()
    {
        if (Environment.GetEnvironmentVariable("TWIST_LOOPBACK") != "1") return;
        var spots = new List<Spot>();
        using var rx = new Receiver();
        rx.SpotDecoded += s => { lock (spots) spots.Add(s); };
        rx.StartDevice(LibTwist.AudioDefault, "default");

        using var b = new Beacon();
        var (dev, rate) = Beacon.FindOutput("");
        var at = DateTime.UtcNow.AddSeconds(3);
        at = at.AddTicks(-(at.Ticks % TimeSpan.TicksPerSecond));                // on a whole second
        b.Start([TwlStep.Frame(TwlProto.TwistS, "F6FBB")], new TxOutput(dev, rate, 1500, 0.5, 120), "test", atUtc: at);
        bool sawWaiting = false, sawPtt = false;
        var until = DateTime.UtcNow.AddSeconds(60);
        while (DateTime.UtcNow < until)
        {
            var st = b.Poll();
            sawWaiting |= st.Waiting;
            sawPtt |= st.Ptt && !st.Waiting;
            lock (spots) if (spots.Count > 0 && !b.Active) break;
            Thread.Sleep(100);
        }
        Assert.True(sawWaiting && sawPtt);
        var s = Assert.Single(spots);
        Assert.Equal((TwlProto.TwistS, "F6FBB"), (s.Proto, s.Call));
        // the decoded start against the asked second: the output latency
        // is absorbed by the timed start, the input one by the receiver
        // (measured through a PulseAudio null sink: -0.03 s, instead of
        // +1.9 s when the output latency was not counted)
        double err = (s.StartUtc - at).TotalSeconds;
        Assert.True(Math.Abs(err) < 0.15, $"start {s.StartUtc:HH:mm:ss.fff} for {at:HH:mm:ss.fff}: {err:0.000} s");
    }
}
