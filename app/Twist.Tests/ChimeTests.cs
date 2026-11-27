using Twist.App.Services;
using Xunit;

namespace Twist.Tests;

public class ChimeTests
{
    [Fact]
    public void Chime_is_a_short_wav_without_clipping()
    {
        var b = Chime.Wav();
        Assert.Equal("RIFF", System.Text.Encoding.ASCII.GetString(b, 0, 4));
        Assert.Equal("WAVE", System.Text.Encoding.ASCII.GetString(b, 8, 4));
        Assert.Equal(b.Length - 8, BitConverter.ToInt32(b, 4));
        int data = BitConverter.ToInt32(b, 40);
        Assert.Equal(b.Length - 44, data);
        Assert.InRange(data / 2 / 22050.0, 0.2, 0.3);                        // a quarter of a second
        var peak = Enumerable.Range(0, data / 2).Max(i => Math.Abs((int)BitConverter.ToInt16(b, 44 + 2 * i)));
        Assert.InRange(peak, 8000, 16000);                                    // heard, far from full scale
        Assert.True(Math.Abs((int)BitConverter.ToInt16(b, 44)) < 200);       // starts with a fade
    }

    [Fact]
    public void Version_alone_for_the_user_agent()
    {
        Assert.Matches(@"^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$", Report.Version);
        Assert.Equal("TWIST Console " + Report.Version, Report.AppVersion);
        Assert.Equal("TWIST-Console/" + Report.Version, Fbbspots.UserAgent);
    }
}
