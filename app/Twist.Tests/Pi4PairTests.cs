using Twist.App.Services;
using Twist.App.ViewModels;
using Xunit;

namespace Twist.Tests;

// A PI4 "/locator" message and a callsign frame of the same beacon: whole
// minutes apart (1 to 3), within 6 Hz.
public class Pi4PairTests
{
    [Theory]
    [InlineData(60.0, 0.0, true)]
    [InlineData(-60.0, 2.0, true)]      // the locator before the callsign
    [InlineData(61.9, 4.0, true)]
    [InlineData(120.0, 0.0, true)]
    [InlineData(180.5, 5.5, true)]
    [InlineData(64.0, 0.0, false)]      // not on the minute: another beacon
    [InlineData(30.0, 0.0, false)]
    [InlineData(240.0, 0.0, false)]     // too long ago
    [InlineData(60.0, 7.0, false)]      // another frequency
    [InlineData(0.5, 0.0, false)]       // the same minute
    public void Same_beacon(double dt, double df, bool same) =>
        Assert.Equal(same, MainWindowViewModel.Pi4Pair(dt, df) is not null);

    [Fact]
    public void Closest_frequency_then_nearest_minute()
    {
        Assert.True(MainWindowViewModel.Pi4Pair(60, 1) < MainWindowViewModel.Pi4Pair(60, 3));
        Assert.True(MainWindowViewModel.Pi4Pair(60, 1) < MainWindowViewModel.Pi4Pair(120, 1));
    }
}

// The live capture watched: the computer asleep, a stream that gives nothing.
public class CaptureWatchTests
{
    [Fact]
    public void Woke_stalled_or_fine()
    {
        var t = new DateTime(2026, 11, 5, 12, 0, 0, DateTimeKind.Utc);
        var w = new CaptureWatch(0, t);
        double sec = 0;
        for (int i = 1; i <= 100; i++)                  // 10 s of audio arriving
            Assert.Equal(CaptureWatch.Verdict.Ok, w.Check(sec += 0.1, t.AddSeconds(0.1 * i), 0.1));
        t = t.AddSeconds(10);
        // asleep for an hour
        Assert.Equal(CaptureWatch.Verdict.Woke, w.Check(sec, t.AddHours(1), 3600));
        // a long pass (the loop late, not the sleep): audio still arrives
        w = new CaptureWatch(sec, t);
        Assert.Equal(CaptureWatch.Verdict.Ok, w.Check(sec += 12, t.AddSeconds(12), 0.1));
        // nothing arrives any more: stalled after 5 s
        t = t.AddSeconds(12);
        for (int i = 1; i <= 50; i++)
            Assert.Equal(CaptureWatch.Verdict.Ok, w.Check(sec, t.AddSeconds(0.1 * i), 0.1));
        Assert.Equal(CaptureWatch.Verdict.Stalled, w.Check(sec, t.AddSeconds(5.2), 0.1));
    }
}
