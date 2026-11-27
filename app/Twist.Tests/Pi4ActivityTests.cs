using Twist.App.Services;
using Xunit;
using Xunit.Abstractions;

namespace Twist.Tests;

// "PI4 frame in progress" (Services/Pi4Activity): at the carrier of the
// frame while it is sent, nothing on the carrier and CW identification
// that follow it, nor in noise.
public class Pi4ActivityTests(ITestOutputHelper output)
{
    [Fact]
    public void Real_recording_frames_at_their_carrier()
    {
        var (x, fs) = Wav.ReadMono("pi4_f5zzo.wav");
        // F5ZZO from 1.4 to 25.7 s, /JN12AX from 61.4 to 85.7 s
        foreach (double end in new[] { 10.0, 18, 25, 70, 80, 85 })
        {
            var c = Pi4Activity.Find(x, (int)(end * fs), fs, 800, 150);
            output.WriteLine($"{end} s: {string.Join(" ", c.Select(f => f.ToString("0")))}");
            Assert.Single(c);
            Assert.InRange(c[0], 790, 806);
        }
        // the end of the minute: carrier, CW identification
        for (double end = 36; end <= 60; end += 3)
        {
            var c = Pi4Activity.Find(x, (int)(end * fs), fs, 800, 150);
            output.WriteLine($"{end} s: {string.Join(" ", c.Select(f => f.ToString("0")))}");
            Assert.Empty(c);
        }
    }

    [Fact]
    public void Nothing_in_noise_nor_on_a_carrier()
    {
        var rnd = new Random(7);
        int fs = 12000, n = 9 * fs, hits = 0;
        var x = new float[n];
        for (int trial = 0; trial < 200; trial++)
        {
            for (int i = 0; i < n; i++)
            {
                double g = Math.Sqrt(-2 * Math.Log(1 - rnd.NextDouble())) * Math.Cos(2 * Math.PI * rnd.NextDouble());
                x[i] = (float)(0.1 * g + (trial % 2 == 1 ? 0.3 * Math.Sin(2 * Math.PI * 1384 * i / fs) : 0));
            }
            hits += Pi4Activity.Find(x, n, fs, 800, 150).Count;
        }
        Assert.Equal(0, hits);
    }
}
