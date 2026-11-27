using Twist.App.Services;
using Twist.Native;
using Xunit;
using Xunit.Abstractions;

namespace Twist.Tests;

// Outils › WAV de test: each kind of frame written in noise, read back by
// the decoder - the frame, its time, its frequency and the SNR asked.
public class TestWavTests(ITestOutputHelper output)
{
    [Theory]
    [InlineData(TwlProto.TwistL, "F6FBB", "JN12AX", 1500, 12000)]
    [InlineData(TwlProto.TwistS, "F1HDI", "", 1430, 48000)]
    [InlineData(TwlProto.Classic, "F5ZZO", "", 1550, 11025)]
    [InlineData(TwlProto.Pi4, "F5ZZO", "", 800, 12000)]
    [InlineData(TwlProto.Pi4, "", "JN12AX", 760, 48000)]
    public void Written_then_decoded(TwlProto p, string call, string loc, double freq, int rate)
    {
        var r = new TestWav.Request(p, call, loc, freq, -15, 1.5, 2, rate, Seed: 3);
        var y = TestWav.Make(r);
        // (plus the last 5 ms ramp down)
        Assert.Equal((int)Math.Round((1.5 + TestWav.FrameSeconds(r) + 2 + 0.005) * rate), y.Length, 2.0);
        var spots = TestWav.Check(y, r);
        foreach (var s in spots) output.WriteLine($"{s.Proto} {s.Call} {s.Loc} {s.Start:0.000} s {s.Freq:0.0} Hz {s.Snr:0.0} dB");
        var f = Assert.Single(spots);
        Assert.Equal(p, f.Proto);
        Assert.Equal(call, f.Call);
        if (p == TwlProto.TwistL || call == "") Assert.Equal(loc, f.Loc);
        Assert.InRange(f.Start, 1.4, 1.6);
        Assert.InRange(f.Freq, freq - 2, freq + 2);
        Assert.InRange(f.Snr, -16.5, -13.5);      // as the list of spots will show it
    }

    [Fact]
    public void Clean_and_loud_files_stay_below_full_scale()
    {
        foreach (double? snr in new double?[] { null, 30 })
        {
            var y = TestWav.Make(new TestWav.Request(TwlProto.TwistS, "F6FBB", "", 1500, snr, 0.5, 0.5, 12000, Seed: 1));
            Assert.InRange(y.Max(Math.Abs), 0.3f, 0.95f);
        }
    }

    [Fact]
    public void Message_too_long_refused()
    {
        var r = new TestWav.Request(TwlProto.Pi4, "F5ZZO/P12", "", 800, -10, 1, 1, 12000);
        Assert.True(TestWav.FrameSeconds(r) < 0);
        Assert.Throws<ArgumentException>(() => TestWav.Make(r));
    }
}
