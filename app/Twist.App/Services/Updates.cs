using System.Diagnostics;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text.Json;
using Twist.App.Lang;

namespace Twist.App.Services;

// A file of a version: its name (beside latest.json), size and SHA-256;
// Base: the update installer applies only over this version or newer.
public sealed record UpdateFile(string File, long Size, string Sha256, string? Base = null);

// What latest.json says of the newest version (docs/WINDOWS.md, "Publishing
// a version"): written by the CI on each published release, copied by
// hand to update.f6fbb.org/twist/ with the installers.
//   { "version": "0.25.0", "date": "2026-11-10", "notes": "### Added ...",
//     "page": "https://update.f6fbb.org/twist/",
//     "windows": { "setup":  { "file": "...-setup.exe", "size": n, "sha256": "..." },
//                  "update": { "file": "...-mise-a-jour.exe", "size": n, "sha256": "...", "base": "0.22.0" } } }
public sealed record UpdateInfo(string Version, string Date, string Notes, string Page, UpdateFile? Setup, UpdateFile? Update);

// New versions (Aide › Rechercher une mise à jour, and by itself: at start
// and once a day, Réglages › Interface): reads latest.json - nothing is
// sent but the request itself (the program's name and version in its
// User-Agent) -, downloads the installer that fits (the small update over
// a recent enough version, else the whole one), checks its size and
// SHA-256, and starts it (Windows: silent, it closes TWIST Console and starts
// it again when done). Elsewhere: the page, to download by hand.
public sealed class Updates
{
    public const string DefaultUrl = "https://update.f6fbb.org/twist/latest.json";
    static readonly System.Text.RegularExpressions.Regex SafeName = new("^[A-Za-z0-9._-]{1,120}$"),
                                                         Hex = new("^[0-9a-f]{64}$");

    public HttpClient Http { get; set; } = NewClient();
    public string Url { get; set; } = DefaultUrl;

    static HttpClient NewClient()
    {
        var h = new HttpClient { Timeout = TimeSpan.FromMinutes(10) };
        h.DefaultRequestHeaders.UserAgent.ParseAdd("TWIST-Console/" + Report.Version);
        return h;
    }

    // the newest version announced (null: the file says nothing usable)
    public async Task<UpdateInfo?> CheckAsync(CancellationToken ct = default)
    {
        using var r = await Http.GetAsync(Url, ct).ConfigureAwait(false);
        r.EnsureSuccessStatusCode();
        return Parse(await r.Content.ReadAsStringAsync(ct).ConfigureAwait(false));
    }

    public static UpdateInfo? Parse(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;
        string S(JsonElement e, string name) =>
            e.ValueKind == JsonValueKind.Object && e.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString()! : "";
        UpdateFile? F(JsonElement e)
        {
            if (e.ValueKind != JsonValueKind.Object) return null;
            string file = S(e, "file"), sha = S(e, "sha256").ToLowerInvariant();
            long size = e.TryGetProperty("size", out var z) && z.TryGetInt64(out var n) ? n : 0;
            // a bare file name only (nothing that leaves the folder of latest.json)
            if (!SafeName.IsMatch(file) || file.Contains("..") || !Hex.IsMatch(sha) || size <= 0) return null;
            return new UpdateFile(file, size, sha, S(e, "base") is { Length: > 0 } b ? b : null);
        }
        var version = S(root, "version");
        if (Version(version) is null) return null;
        root.TryGetProperty("windows", out var win);
        JsonElement setup = default, update = default;
        if (win.ValueKind == JsonValueKind.Object)
        {
            win.TryGetProperty("setup", out setup);
            win.TryGetProperty("update", out update);
        }
        return new UpdateInfo(version, S(root, "date"), S(root, "notes"), S(root, "page"), F(setup), F(update));
    }

    // "0.24.1" -> (0, 24, 1, ""), "1.0.0-rc.1" -> (1, 0, 0, "rc.1"); null
    // if not of that form (a "+build" part is ignored)
    static (int, int, int, string)? Version(string v)
    {
        v = v.Trim().TrimStart('v').Split('+')[0];
        var dash = v.IndexOf('-');
        string pre = dash < 0 ? "" : v[(dash + 1)..];
        var p = (dash < 0 ? v : v[..dash]).Split('.');
        if (p.Length != 3 || (dash >= 0 && pre == "")) return null;
        return int.TryParse(p[0], out var a) && int.TryParse(p[1], out var b) && int.TryParse(p[2], out var c) ? (a, b, c, pre) : null;
    }

    // Semantic versioning: 1.0.0-rc.1 < 1.0.0-rc.2 < 1.0.0 < 1.0.1; the
    // parts of a pre-release compared one by one, numbers as numbers
    static int Compare((int, int, int, string) x, (int, int, int, string) y)
    {
        int c = (x.Item1, x.Item2, x.Item3).CompareTo((y.Item1, y.Item2, y.Item3));
        if (c != 0) return c;
        if (x.Item4 == y.Item4) return 0;
        if (x.Item4 == "") return 1;                     // the release after its pre-releases
        if (y.Item4 == "") return -1;
        var a = x.Item4.Split('.');
        var b = y.Item4.Split('.');
        for (int i = 0; i < Math.Min(a.Length, b.Length); i++)
        {
            bool na = int.TryParse(a[i], out var ia), nb = int.TryParse(b[i], out var ib);
            c = na && nb ? ia.CompareTo(ib) : na ? -1 : nb ? 1 : string.CompareOrdinal(a[i], b[i]);
            if (c != 0) return c;
        }
        return a.Length.CompareTo(b.Length);
    }

    // a is newer than b
    public static bool Newer(string a, string b) =>
        Version(a) is { } x && Version(b) is { } y && Compare(x, y) > 0;

    // the installer to take from `current`: the update when it applies (its
    // base not newer than current), else the whole installer
    public static UpdateFile? Choose(UpdateInfo i, string current) =>
        i.Update is { Base: { } b } u && !Newer(b, current) ? u : i.Setup;

    // downloads `f` (beside latest.json) into `folder`, checks its size and
    // SHA-256: the path, or an exception (the file is then removed)
    public async Task<string> DownloadAsync(UpdateFile f, string folder, IProgress<double>? progress = null, CancellationToken ct = default)
    {
        var uri = new Uri(new Uri(Url), f.File);
        Directory.CreateDirectory(folder);
        var path = Path.Combine(folder, f.File);
        try
        {
            using (var r = await Http.GetAsync(uri, HttpCompletionOption.ResponseHeadersRead, ct).ConfigureAwait(false))
            {
                r.EnsureSuccessStatusCode();
                await using var src = await r.Content.ReadAsStreamAsync(ct).ConfigureAwait(false);
                await using var dst = File.Create(path);
                var buf = new byte[81920];
                long got = 0;
                int n;
                while ((n = await src.ReadAsync(buf, ct).ConfigureAwait(false)) > 0)
                {
                    if ((got += n) > f.Size) throw new InvalidDataException(L.T("fichier plus long qu'annoncé"));
                    await dst.WriteAsync(buf.AsMemory(0, n), ct).ConfigureAwait(false);
                    progress?.Report((double)got / f.Size);
                }
            }
            if (new FileInfo(path).Length != f.Size) throw new InvalidDataException(L.T("fichier incomplet"));
            string sha;
            await using (var s = File.OpenRead(path))
                sha = Convert.ToHexString(await SHA256.HashDataAsync(s, ct).ConfigureAwait(false)).ToLowerInvariant();
            if (sha != f.Sha256) throw new InvalidDataException(L.T("empreinte SHA-256 différente"));
            return path;
        }
        catch
        {
            try { File.Delete(path); } catch { }
            throw;
        }
    }

    // starts the installer, silent: it closes TWIST Console (Restart Manager)
    // and, with /RELAUNCH, starts it again when done (installer/*.iss)
    public static void Launch(string installer)
    {
        // (through the shell: an installation for all users asks for the
        // administrator's rights, the installer elevating itself)
        Process.Start(new ProcessStartInfo(installer, "/SILENT /SUPPRESSMSGBOXES /CLOSEAPPLICATIONS /RELAUNCH") { UseShellExecute = true });
    }
}
