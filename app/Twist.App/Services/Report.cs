using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json.Nodes;
using Twist.Native;
using Twist.App.Lang;

namespace Twist.App.Services;

// Aide › Créer un rapport: what a tester sends back, in one zip in the
// log folder (rapports\twist-rapport-<date>-<time>Z.zip):
//   lisez-moi.txt   versions, system, audio devices, Hamlib, state now
//   journal.txt     the messages of the session (up to 5000 lines)
//   twist.json      the settings, without the MQTT password
//   twist-error.log the error log (its end, 1 MB at most)
//   spots-*.csv     today's spots journal
//   *.wav           the newest kept WAV, if any (Réglages › Rapports)
public static class Report
{
    // "0.19.0", and "TWIST Console 0.19.0" (window title, report, the
    // "software" of each spot sent): the <Version> of Twist.App.csproj, as
    // the assembly carries it (AssemblyInformationalVersion, without the
    // "+<git hash>" the .NET SDK adds), so it follows every release
    public static string Version { get; } = ReadVersion();
    public static string AppVersion => "TWIST Console " + Version;

    static string ReadVersion()
    {
        var a = typeof(Report).Assembly;
        var info = System.Reflection.CustomAttributeExtensions
            .GetCustomAttribute<System.Reflection.AssemblyInformationalVersionAttribute>(a)?.InformationalVersion;
        if (info is { Length: > 0 }) return Clean(info);
        var v = a.GetName().Version;
        return $"{v?.Major}.{v?.Minor}.{v?.Build}";
    }

    // "0.27.1+3f2a9c..." -> "0.27.1"; printable ASCII only
    public static string Clean(string informational)
    {
        var v = informational.Split('+')[0].Trim();
        return new string(v.Where(c => c > ' ' && c < 127).ToArray());
    }

    // the program that made a spot, for fbbspots (spots.software, shown on
    // the Reporter cell of spots.f6fbb.org): printable ASCII, 48 at most
    public static string Software
    {
        get
        {
            var s = new string(AppVersion.Where(c => c >= ' ' && c < 127).ToArray());
            return s.Length > 48 ? s[..48] : s;
        }
    }

    public static string Create(string folder, string settingsJson, IEnumerable<string> journal, string state,
                                string? csv, string? wav)
    {
        var dir = Path.Combine(folder, "rapports");
        Directory.CreateDirectory(dir);
        var path = Path.Combine(dir, $"twist-rapport-{DateTime.UtcNow:yyyyMMdd-HHmmss}Z.zip");
        using var zip = ZipFile.Open(path, ZipArchiveMode.Create);

        void Text(string name, string content)
        {
            var e = zip.CreateEntry(name, CompressionLevel.Optimal);
            using var w = new StreamWriter(e.Open(), new UTF8Encoding(true));
            w.Write(content.Replace("\r\n", "\n").Replace("\n", "\r\n"));      // readable in Notepad
        }

        Text("lisez-moi.txt", Info() + "\n" + state);
        Text("journal.txt", string.Join("\n", journal) + "\n");
        Text("twist.json", WithoutSecrets(settingsJson));
        if (File.Exists(Program.ErrorLog))
        {
            var b = File.ReadAllBytes(Program.ErrorLog);
            if (b.Length > (1 << 20)) b = b[^(1 << 20)..];
            Text("twist-error.log", Encoding.UTF8.GetString(b));
        }
        if (csv is not null && File.Exists(csv)) zip.CreateEntryFromFile(csv, "spots-" + Path.GetFileName(csv));
        if (wav is not null && File.Exists(wav) && new FileInfo(wav).Length < (20 << 20))
            zip.CreateEntryFromFile(wav, Path.GetFileName(wav), CompressionLevel.Optimal);
        return path;
    }

    static string WithoutSecrets(string json)
    {
        try
        {
            var n = JsonNode.Parse(json)!;
            if (n["reports"] is JsonObject r)
                foreach (var k in new[] { "mqttPassword", "fbbspotsKey" })
                    if (r[k] is not null) r[k] = (string?)r[k] == "" ? "" : L.T("(retiré du rapport)");
            return n.ToJsonString(new System.Text.Json.JsonSerializerOptions
            {
                WriteIndented = true,
                Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
            });
        }
        catch { return L.T("(réglages illisibles)"); }
    }

    static string Info()
    {
        var sb = new StringBuilder();
        sb.AppendLine(L.F("{0} — rapport du {1:yyyy-MM-dd HH:mm:ss} UTC ({2:HH:mm:ss} heure locale)", AppVersion, DateTime.UtcNow, DateTime.Now));
        sb.AppendLine(L.F("Système : {0} {1}, .NET {2}", RuntimeInformation.OSDescription, RuntimeInformation.OSArchitecture, Environment.Version));
        sb.AppendLine(L.F("Programme : {0}", Environment.ProcessPath));
        try { sb.AppendLine(L.F("Bibliothèques : libtwcore, libtwist {0} (API {1}) ; libopera {2} ; libpi4 {3}", LibTwist.Version, LibTwist.GetApiVersion(),
            LibTwist.HasClassic ? L.T("présente") : L.T("absente"), LibTwist.HasPi4 ? L.T("présente") : L.T("absente"))); }
        catch (Exception e) { sb.AppendLine(L.T("Moteur : absent — ") + e.Message); }
        sb.AppendLine($"Hamlib : {Hamlib.Tool("rigctld") ?? L.T("rigctld introuvable")}");
        sb.AppendLine();
        sb.AppendLine(L.T("Périphériques audio (index, nom, API, entrées, sorties, fréquence) :"));
        try
        {
            var devs = TwistAudio.Devices();
            for (int i = 0; i < devs.Length; i++)
            {
                var d = devs[i];
                sb.AppendLine($"  {i,2}  {d.Name} ({d.HostApi})  in {d.MaxIn}  out {d.MaxOut}  {d.DefaultRate:0} Hz" +
                              (d.IsDefaultIn != 0 ? L.T("  [entrée par défaut]") : "") + (d.IsDefaultOut != 0 ? L.T("  [sortie par défaut]") : ""));
            }
        }
        catch (Exception e) { sb.AppendLine(L.T("  (liste impossible : ") + e.Message + ")"); }
        return sb.ToString();
    }
}
