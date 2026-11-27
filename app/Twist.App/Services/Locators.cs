using System.Net.Http;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Twist.App.Services;

// Where a locator shown for a spot comes from.
public enum LocSource { Frame, List, Heard, Fbbspots }

public sealed record LocatorFound(string Locator, LocSource Source, DateTime Utc, bool Approximate = false);

// The locator of a callsign received without one (OPERA 05, TWIST S),
// from, in this order:
//   1. the list of the settings (Station › Indicatifs connus: "F5ZZO JN12AX, F1HDI");
//   2. the TWIST L frames already received from it (kept in locators.json);
//   3. fbbspots (spots.f6fbb.org/locator), if allowed: one question per
//      callsign, its answer kept 30 days (locators.json), an unknown one
//      not asked again for 6 hours.
// A locator received in the frame itself is always the one shown; these
// are only for the frames without one, and are never sent on (MQTT, CSV).
// As on the fbbalise site: received in capitals, guessed in lower case
// (these are all guessed: JN12AX received, jn12ax found).
public sealed class LocatorBook
{
    public const string DefaultApi = "https://spots.f6fbb.org/locator";
    static readonly Regex LocRe = new("^[A-R]{2}[0-9]{2}([A-X]{2})?$", RegexOptions.IgnoreCase);
    static readonly Regex CallRe = new("^[A-Z0-9/]{3,16}$");
    static readonly TimeSpan KeepFound = TimeSpan.FromDays(30), RetryUnknown = TimeSpan.FromHours(6),
                             AfterFailure = TimeSpan.FromMinutes(10);

    readonly object _lock = new();
    readonly string? _file;
    Dictionary<string, string> _list = new();
    readonly Dictionary<string, Entry> _heard = new(), _found = new();
    readonly Dictionary<string, DateTime> _unknown = new();
    readonly HashSet<string> _asking = new();
    DateTime _failedUntil;

    sealed class Entry
    {
        public string Locator { get; set; } = "";
        public DateTime Utc { get; set; }
        public bool Approximate { get; set; }
    }
    sealed class Store
    {
        public Dictionary<string, Entry> Heard { get; set; } = new();
        public Dictionary<string, Entry> Fbbspots { get; set; } = new();
    }

    public HttpClient Http { get; set; } = NewClient();
    static HttpClient NewClient()
    {
        var h = new HttpClient { Timeout = TimeSpan.FromSeconds(8) };
        h.DefaultRequestHeaders.UserAgent.ParseAdd(Fbbspots.UserAgent);      // TWIST-Console/<version>
        return h;
    }
    public string Api { get; set; } = DefaultApi;
    public bool AskFbbspots { get; set; } = true;
    public event Action<string>? Message;

    // file: locators.json beside the settings (null: in memory only)
    public LocatorBook(string? file)
    {
        _file = file;
        if (file is null || !File.Exists(file)) return;
        try
        {
            var s = JsonSerializer.Deserialize<Store>(File.ReadAllText(file));
            if (s is null) return;
            foreach (var (k, v) in s.Heard) if (LocRe.IsMatch(v.Locator)) _heard[k] = v;
            foreach (var (k, v) in s.Fbbspots) if (LocRe.IsMatch(v.Locator)) _found[k] = v;
        }
        catch { /* a damaged file: started again */ }
    }

    public static bool IsLocator(string s) => LocRe.IsMatch(s);

    // "F5ZZO JN12AX, F1HDI; F6XYZ" -> (F5ZZO, JN12AX), (F1HDI, null), (F6XYZ, null):
    // a locator follows the callsign it belongs to
    public static List<(string call, string? loc)> ParseList(string text)
    {
        var list = new List<(string call, string? loc)>();
        foreach (var t in text.ToUpperInvariant().Split(new[] { ',', ';', ' ', '\t', '\n', '\r', '=' }, StringSplitOptions.RemoveEmptyEntries))
        {
            if (list.Count > 0 && list[^1].loc is null && LocRe.IsMatch(t)) { list[^1] = (list[^1].call, t); continue; }
            if (!CallRe.IsMatch(t) || list.Any(x => x.call == t)) continue;
            list.Add((t, null));
        }
        return list;
    }

    // the list as it is written back to the settings, and the callsigns
    // only (for the deep search of the decoder)
    public static string FormatList(List<(string call, string? loc)> list) =>
        string.Join(", ", list.Select(x => x.loc is null ? x.call : $"{x.call} {x.loc.ToUpperInvariant()}"));
    public static string CallsOnly(string text) => string.Join(",", ParseList(text).Select(x => x.call));

    public void SetList(string text)
    {
        var d = new Dictionary<string, string>();
        foreach (var (c, l) in ParseList(text)) if (l is not null) d[c] = l;
        lock (_lock) _list = d;
    }

    static string Guessed(string loc) => loc.ToLowerInvariant();

    // a TWIST L frame received: its locator remembered
    public void Heard(string call, string loc)
    {
        if (!LocRe.IsMatch(loc)) return;
        lock (_lock)
        {
            _heard[call.ToUpperInvariant()] = new Entry { Locator = Guessed(loc), Utc = DateTime.UtcNow };
            _unknown.Remove(call.ToUpperInvariant());
        }
        Save();
    }

    // what is known now, without asking anyone
    public LocatorFound? Find(string call)
    {
        call = call.ToUpperInvariant();
        lock (_lock)
        {
            if (_list.TryGetValue(call, out var l)) return new(Guessed(l), LocSource.List, DateTime.MinValue);
            if (_heard.TryGetValue(call, out var h)) return new(h.Locator, LocSource.Heard, h.Utc);
            if (_found.TryGetValue(call, out var s) && DateTime.UtcNow - s.Utc < KeepFound)
                return new(s.Locator, LocSource.Fbbspots, s.Utc, s.Approximate);
        }
        return null;
    }

    // Find, else fbbspots (if allowed and not asked recently); null: unknown
    public async Task<LocatorFound?> LookupAsync(string call, int bandMHz)
    {
        call = call.ToUpperInvariant();
        if (Find(call) is { } f) return f;
        if (!AskFbbspots || !CallRe.IsMatch(call)) return null;
        lock (_lock)
        {
            if (DateTime.UtcNow < _failedUntil) return null;
            if (_unknown.TryGetValue(call, out var t) && DateTime.UtcNow - t < RetryUnknown) return null;
            if (!_asking.Add(call)) return null;           // already being asked
        }
        try
        {
            var url = $"{Api}?call={Uri.EscapeDataString(call)}" + (bandMHz > 0 ? $"&band={bandMHz}" : "");
            using var r = await Http.GetAsync(url).ConfigureAwait(false);
            if (r.StatusCode == System.Net.HttpStatusCode.NotFound)
            {
                lock (_lock) _unknown[call] = DateTime.UtcNow;
                return null;
            }
            r.EnsureSuccessStatusCode();
            using var doc = JsonDocument.Parse(await r.Content.ReadAsStringAsync().ConfigureAwait(false));
            var root = doc.RootElement;
            var loc = root.TryGetProperty("locator", out var lv) && lv.ValueKind == JsonValueKind.String ? lv.GetString()! : "";
            if (!LocRe.IsMatch(loc))
            {
                lock (_lock) _unknown[call] = DateTime.UtcNow;
                return null;
            }
            bool approx = root.TryGetProperty("approximate", out var av) && av.ValueKind == JsonValueKind.True;
            var e = new Entry { Locator = Guessed(loc), Utc = DateTime.UtcNow, Approximate = approx };
            lock (_lock) _found[call] = e;
            Save();
            return new(e.Locator, LocSource.Fbbspots, e.Utc, approx);
        }
        catch (Exception ex)
        {
            lock (_lock) _failedUntil = DateTime.UtcNow + AfterFailure;
            Message?.Invoke(Lang.L.F("recherche du locator de {0} impossible ({1}) : nouvel essai dans 10 min", call, ex.Message));
            return null;
        }
        finally
        {
            lock (_lock) _asking.Remove(call);
        }
    }

    void Save()
    {
        if (_file is null) return;
        try
        {
            string json;
            lock (_lock) json = JsonSerializer.Serialize(new Store { Heard = new(_heard), Fbbspots = new(_found) },
                                                         new JsonSerializerOptions { WriteIndented = true });
            Directory.CreateDirectory(Path.GetDirectoryName(_file)!);
            var tmp = _file + ".tmp";
            File.WriteAllText(tmp, json);
            File.Move(tmp, _file, overwrite: true);
        }
        catch { /* only a cache */ }
    }
}
