using System.Globalization;
using System.Text;

namespace Twist.App.Services;

// The spots journal: one CSV file per UTC day in the log folder,
// twist-YYYY-MM-DD.csv, one line per decoded frame, appended as soon as
// it is decoded (nothing lost if the program stops).
//
// Separator ';' and decimal point '.': the file opens in a French
// spreadsheet (Excel, LibreOffice: choose ';' if asked) and reads the
// same everywhere else. UTF-8 with a BOM so that Excel shows the
// accents of the header.
//
//   date;heure_utc;protocole;indicatif;locator;snr_db;audio_hz;rf_mhz;distance_km;azimut;profond;source;douteux
//   2026-10-28;14:14:00;TWIST L;F5ZZO;JN03TJ;-12;1549;144.490549;204;178;0;CABLE Output (VB-Audio);
// douteux: why the callsign may be a ghost (Services/CallCheck), empty if not.
public sealed class CsvLog
{
    public const string Header = "date;heure_utc;protocole;indicatif;locator;snr_db;audio_hz;rf_mhz;distance_km;azimut;profond;source;douteux";
    static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

    public string Folder { get; set; }

    public CsvLog(string folder) => Folder = folder;

    public string PathFor(DateTime utc) => Path.Combine(Folder, $"twist-{utc:yyyy-MM-dd}.csv");

    // Appends one spot; returns the file written. Throws on an I/O error
    // (the caller reports it in the journal).
    public string Append(Spot s, string protoLabel, double? rfMHz, double? km, double? az, string source, string? doubt = null)
    {
        Directory.CreateDirectory(Folder);
        var path = PathFor(s.StartUtc);
        bool fresh = !File.Exists(path) || new FileInfo(path).Length == 0;
        var line = string.Join(';',
            s.StartUtc.ToString("yyyy-MM-dd", Inv),
            s.StartUtc.ToString("HH:mm:ss", Inv),
            Field(protoLabel),
            Field(s.Call),
            Field(s.Loc),
            Math.Round(s.Snr).ToString("0", Inv),
            s.FreqHz.ToString("0", Inv),
            rfMHz is double f ? f.ToString("0.000000", Inv) : "",
            km is double k ? k.ToString("0", Inv) : "",
            az is double a ? a.ToString("0", Inv) : "",
            s.Deep ? "1" : "0",
            Field(source),
            Field(doubt ?? ""));
        using var w = new StreamWriter(path, append: true, new UTF8Encoding(encoderShouldEmitUTF8Identifier: fresh));
        if (fresh) w.WriteLine(Header);
        w.WriteLine(line);
        return path;
    }

    // a field with the separator, a quote or a line break is quoted
    static string Field(string v) =>
        v.IndexOfAny([';', '"', '\n', '\r']) < 0 ? v : "\"" + v.Replace("\"", "\"\"") + "\"";
}
