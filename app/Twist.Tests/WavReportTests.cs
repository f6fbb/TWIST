using System.IO.Compression;
using System.Text;
using Twist.App.Services;
using Twist.Native;
using Xunit;

namespace Twist.Tests;

public class WavReportTests
{
    static string TempDir()
    {
        var d = Path.Combine(Path.GetTempPath(), "twist-tests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(d);
        return d;
    }

    [Fact]
    public void Kept_wav_named_readable_and_still_decodes()
    {
        var (x, fs) = Wav.ReadMono("f5zzo.wav");
        var dir = TempDir();
        var k = new WavKeeper(dir);
        var t0 = new DateTime(2026, 10, 28, 14, 13, 30, DateTimeKind.Utc);
        var spot = new Spot(t0.AddSeconds(5), TwlProto.Classic, "F5ZZO", "", -12, 1564, false);
        var path = k.Save(x, fs, t0, 144.489, [(spot, "classic")], "TWIST Console test");
        Assert.EndsWith(Path.Combine("wav", "2026-10-28", "twist-20261028-141330Z-144.489000MHz.wav"), path);

        var (y, fs2) = Wav.ReadMono(path);
        Assert.Equal(fs, fs2);
        Assert.Equal(x.Length, y.Length);
        Assert.True(x.Zip(y).Max(p => Math.Abs(p.First - p.Second)) < 1.0 / 32000);   // 16-bit
        Assert.Contains("F5ZZO", Encoding.UTF8.GetString(File.ReadAllBytes(path)[^300..]));  // comment after the data

        using var dec = new TwistDecoder();
        LibTwist.SettingsDefault(out var s);
        Assert.Contains(dec.Decode(y, fs2, s), p => p.Call == "F5ZZO");
    }

    // WAVE_FORMAT_EXTENSIBLE, 32-bit float stereo (as many recorders
    // write them): read as mono, the mean of the channels
    [Fact]
    public void Extensible_float_stereo_read()
    {
        var path = Path.Combine(TempDir(), "ext.wav");
        float[] l = [0.5f, -0.25f, 1f], r = [0.25f, -0.25f, 0f];
        using (var w = new BinaryWriter(File.Create(path)))
        {
            w.Write("RIFF"u8); w.Write(4 + 8 + 40 + 8 + 24); w.Write("WAVE"u8);
            w.Write("fmt "u8); w.Write(40);
            w.Write((ushort)0xFFFE); w.Write((ushort)2); w.Write(48000); w.Write(48000 * 8); w.Write((ushort)8); w.Write((ushort)32);
            w.Write((ushort)22); w.Write((ushort)32); w.Write(3);                       // cbSize, valid bits, channel mask
            w.Write([3, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71]);  // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
            w.Write("data"u8); w.Write(24);
            for (int i = 0; i < 3; i++) { w.Write(l[i]); w.Write(r[i]); }
        }
        var (x, fs) = Wav.ReadMono(path);
        Assert.Equal(48000, fs);
        Assert.Equal([0.375f, -0.25f, 0.5f], x);
    }

    [Fact]
    public void Unsupported_wav_format_says_so()
    {
        var path = Path.Combine(TempDir(), "adpcm.wav");
        using (var w = new BinaryWriter(File.Create(path)))
        {
            w.Write("RIFF"u8); w.Write(4 + 8 + 16 + 8 + 4); w.Write("WAVE"u8);
            w.Write("fmt "u8); w.Write(16);
            w.Write((ushort)2); w.Write((ushort)1); w.Write(8000); w.Write(4000); w.Write((ushort)256); w.Write((ushort)4);
            w.Write("data"u8); w.Write(4); w.Write(0);
        }
        var e = Assert.Throws<InvalidDataException>(() => Wav.ReadMono(path));
        Assert.Contains("code 2", e.Message);
    }

    [Fact]
    public void Oldest_wav_removed_beyond_size_and_age()
    {
        var dir = TempDir();
        var k = new WavKeeper(dir) { Days = 30 };
        var x = new float[24000];
        var t = new DateTime(2026, 10, 28, 10, 0, 0, DateTimeKind.Utc);
        var paths = Enumerable.Range(0, 3).Select(i =>
        {
            var p = k.Save(x, 12000, t.AddMinutes(i), null, [], "t");
            File.SetLastWriteTimeUtc(p, DateTime.UtcNow.AddMinutes(-10 + i));
            return p;
        }).ToList();
        var old = k.Save(x, 12000, t.AddDays(-40), null, [], "t");
        File.SetLastWriteTimeUtc(old, DateTime.UtcNow.AddDays(-40));
        k.MaxBytes = 2 * new FileInfo(paths[0]).Length + 10;   // room for two
        Assert.Equal(2, k.Prune());                      // 40 days old, then the oldest over the size
        Assert.False(File.Exists(old));
        Assert.False(File.Exists(paths[0]));
        Assert.True(File.Exists(paths[1]) && File.Exists(paths[2]));
        Assert.Equal(paths[2], k.Newest());
    }

    [Fact]
    public void Report_zip_has_everything_but_the_password()
    {
        var dir = TempDir();
        var csv = Path.Combine(dir, "twist-2026-10-28.csv");
        File.WriteAllText(csv, CsvLog.Header + "\n");
        var json = "{ \"station\": { \"callsign\": \"F6FBB\" }, \"reports\": { \"mqttUser\": \"u\", \"mqttPassword\": \"dpapi:AAAA\" } }";
        var zipPath = Report.Create(dir, json, ["2026-10-28 14:00:00 capture : test"], "État : test", csv, null);
        using var z = ZipFile.OpenRead(zipPath);
        var names = z.Entries.Select(e => e.FullName).ToList();
        Assert.Contains("lisez-moi.txt", names);
        Assert.Contains("journal.txt", names);
        Assert.Contains("twist.json", names);
        Assert.Contains("spots-twist-2026-10-28.csv", names);
        string Read(string n) { using var r = new StreamReader(z.GetEntry(n)!.Open()); return r.ReadToEnd(); }
        Assert.DoesNotContain("dpapi:AAAA", Read("twist.json"));
        Assert.Contains("F6FBB", Read("twist.json"));
        Assert.Contains("libtwist", Read("lisez-moi.txt"));
        Assert.Contains("État : test", Read("lisez-moi.txt"));
    }
}
