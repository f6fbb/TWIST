using System.Globalization;
using Twist.App.Lang;

namespace Twist.App.Services;

// Keeps the audio of the passes that decoded something (Réglages ›
// Rapports), for analysis or for a testers' report: the block the
// decoder saw (45 s, mono, 16-bit, at the capture rate - 12 kHz),
//   <log folder>\wav\2026-10-28\twist-20261028-141330Z-144.489000MHz.wav
// named by the UTC time of its first sample and the radio frequency; a
// comment inside (INFO chunk) says what was decoded in it. Replayed by
// Fichier › Rejouer, or twistd -T 141330 <file>.
// The oldest files go first beyond WavDays days or WavMaxMB megabytes.
public sealed class WavKeeper
{
    static readonly CultureInfo Inv = CultureInfo.InvariantCulture;
    public string Folder { get; set; }
    public int Days { get; set; } = 30;
    public long MaxBytes { get; set; } = 2000L << 20;
    DateTime _lastPrune = DateTime.MinValue;

    public WavKeeper(string logFolder) => Folder = Path.Combine(logFolder, "wav");

    public string Save(ReadOnlySpan<float> x, int fs, DateTime startUtc, double? radioMHz, IEnumerable<(Spot spot, string label)> spots, string app)
    {
        var dir = Path.Combine(Folder, startUtc.ToString("yyyy-MM-dd", Inv));
        Directory.CreateDirectory(dir);
        var name = $"twist-{startUtc:yyyyMMdd-HHmmss}Z" + (radioMHz is double f ? $"-{f.ToString("0.000000", Inv)}MHz" : "") + ".wav";
        var path = Path.Combine(dir, name);
        var what = string.Join(" ; ", spots.Select(p =>
            $"{p.label} {p.spot.Call}{(p.spot.Loc == "" ? "" : " " + p.spot.Loc)} {Math.Round(p.spot.Snr).ToString("0", Inv)} dB " +
            L.F("{0} Hz début {1}Z", p.spot.FreqHz.ToString("0", Inv), p.spot.StartUtc.ToString("HH:mm:ss", Inv))));
        var comment = L.F("{0} · premier échantillon {1}Z · ", app, startUtc.ToString("yyyy-MM-dd HH:mm:ss.f", Inv)) +
                      (radioMHz is double m ? $"radio {m.ToString("0.000000", Inv)} MHz USB · " : "") +
                      $"{fs} Hz · {what}";
        Wav.WriteMono16(path, x, fs, comment);
        if ((DateTime.UtcNow - _lastPrune).TotalMinutes > 10) Prune();
        return path;
    }

    // removes what is too old, then the oldest until under the size
    public int Prune()
    {
        _lastPrune = DateTime.UtcNow;
        if (!Directory.Exists(Folder)) return 0;
        int removed = 0;
        var files = new DirectoryInfo(Folder).GetFiles("twist-*.wav", SearchOption.AllDirectories)
                                             .OrderBy(f => f.LastWriteTimeUtc).ToList();
        var limit = DateTime.UtcNow.AddDays(-Math.Max(1, Days));
        long total = files.Sum(f => f.Length);
        foreach (var f in files)
        {
            if (f.LastWriteTimeUtc >= limit && total <= MaxBytes) break;
            try { total -= f.Length; f.Delete(); removed++; } catch { }
        }
        foreach (var d in new DirectoryInfo(Folder).GetDirectories())
            try { if (!d.EnumerateFileSystemInfos().Any()) d.Delete(); } catch { }
        return removed;
    }

    // the newest file kept, for a report
    public string? Newest() =>
        Directory.Exists(Folder)
            ? new DirectoryInfo(Folder).GetFiles("twist-*.wav", SearchOption.AllDirectories)
                                       .OrderByDescending(f => f.LastWriteTimeUtc).FirstOrDefault()?.FullName
            : null;
}
