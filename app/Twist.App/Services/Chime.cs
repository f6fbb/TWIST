using System.Diagnostics;
using System.Runtime.InteropServices;

namespace Twist.App.Services;

// Réglages › Interface › Son à la réception d'un spot: two short notes,
// played by the system on its default output (not the sound card of the
// profile, which may be the transmitter's: with VOX, a chime there would
// key it). At most one chime every 3 s, a burst of spots gives one.
public static class Chime
{
    static string? _file;
    static DateTime _last = DateTime.MinValue;
    public static TimeSpan Gap { get; set; } = TimeSpan.FromSeconds(3);

    // a WAV of 16-bit mono, 22 050 Hz: 880 Hz then 1320 Hz, 120 ms each
    public static byte[] Wav()
    {
        const int rate = 22050;
        var notes = new[] { 880.0, 1320.0 };
        int per = rate * 120 / 1000, n = per * notes.Length;
        var ms = new MemoryStream();
        var w = new BinaryWriter(ms);
        w.Write("RIFF"u8); w.Write(36 + n * 2); w.Write("WAVE"u8);
        w.Write("fmt "u8); w.Write(16); w.Write((short)1); w.Write((short)1); w.Write(rate); w.Write(rate * 2);
        w.Write((short)2); w.Write((short)16);
        w.Write("data"u8); w.Write(n * 2);
        for (int k = 0; k < notes.Length; k++)
            for (int i = 0; i < per; i++)
            {
                double t = (double)i / rate;
                double env = Math.Min(1, Math.Min(i, per - i) / (rate * 0.01));     // 10 ms fades: no clicks
                w.Write((short)(Math.Sin(2 * Math.PI * notes[k] * t) * env * 0.35 * short.MaxValue));
            }
        w.Flush();
        return ms.ToArray();
    }

    // false if too soon after the last one (or no way to play it)
    public static bool Play(bool force = false)
    {
        var now = DateTime.UtcNow;
        if (!force && now - _last < Gap) return false;
        _last = now;
        try
        {
            if (_file is null || !File.Exists(_file))
            {
                var f = Path.Combine(Path.GetTempPath(), "twist-spot.wav");
                File.WriteAllBytes(f, Wav());
                _file = f;
            }
            if (OperatingSystem.IsWindows())
                return PlaySound(_file, IntPtr.Zero, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
            if (OperatingSystem.IsMacOS()) return Run("afplay", _file);
            return Run("paplay", _file) || Run("aplay", "-q", _file);
        }
        catch { return false; }
    }

    static bool Run(string tool, params string[] args)
    {
        try
        {
            var psi = new ProcessStartInfo(tool) { UseShellExecute = false, RedirectStandardError = true, RedirectStandardOutput = true };
            foreach (var a in args) psi.ArgumentList.Add(a);
            using var p = Process.Start(psi);
            return p is not null;
        }
        catch { return false; }                      // not installed
    }

    const uint SND_ASYNC = 0x0001, SND_NODEFAULT = 0x0002, SND_FILENAME = 0x00020000;
    [DllImport("winmm.dll", CharSet = CharSet.Unicode)]
    static extern bool PlaySound(string sound, IntPtr module, uint flags);
}
