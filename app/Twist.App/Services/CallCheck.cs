using System.Text.Json;
using Twist.App.Lang;

namespace Twist.App.Services;

// Is a decoded callsign a real one? Near the sensitivity limit a frame
// can pass its CRC with a few wrong groups and give a "ghost" callsign
// (TW6DXY OL30XN...). The engine already drops the prefixes
// allocated to no country (tw_call_plausible); here, two finer checks,
// and the spot is only marked doubtful (greyed, not sent, kept in the
// CSV with the reason):
//   - its prefix must be one used by radio amateurs: a prefix of the
//     country files of AD1C (DXCC entities); for the countries of one
//     letter (F, G, I, K, M, N, R, U, W) whose file entry takes every
//     second letter, the rules of that country (F + letter only for the
//     overseas entities, M/G + letter only the regions and GB, GX, MX)
//   - with a locator (TWIST L): the locator must be in the country of
//     the callsign, or near it (150 km around the locator square), or
//     less than 500 km from the point of its entity (small islands)
// Distance is no criterion: on HF a far spot is a real one.
// The data: Assets/callcheck.json, made by tools/callcheck/gen_callcheck.py.
public static class CallCheck
{
    public sealed record Entity(string Prefix, string Name, double Lat, double Lon, int Country);

    sealed class Data
    {
        public Dictionary<string, int> Prefixes = new();
        public List<Entity> Entities = new();
        public Dictionary<string, int[]> Squares = new();
        public int MaxPrefix;
    }

    static readonly Lazy<Data> _data = new(Load);

    static Data Load()
    {
        var d = new Data();
        using var st = typeof(CallCheck).Assembly.GetManifestResourceStream("Twist.App.callcheck.json")
                       ?? throw new InvalidOperationException("callcheck.json missing");
        using var doc = JsonDocument.Parse(st);
        var r = doc.RootElement;
        foreach (var e in r.GetProperty("entities").EnumerateArray())
            d.Entities.Add(new Entity(e[0].GetString()!, e[1].GetString()!, e[2].GetDouble(), e[3].GetDouble(), e[4].GetInt32()));
        foreach (var p in r.GetProperty("prefixes").EnumerateObject())
        {
            d.Prefixes[p.Name] = p.Value.GetInt32();
            d.MaxPrefix = Math.Max(d.MaxPrefix, p.Name.Length);
        }
        // UK 2O, 2Q, 2R, 2V (like 2E: England unless an exception says)
        if (d.Prefixes.TryGetValue("2E", out var england))
            foreach (var p in new[] { "2O", "2Q", "2R", "2V" }) d.Prefixes.TryAdd(p, england);
        foreach (var s in r.GetProperty("squares").EnumerateObject())
            d.Squares[s.Name] = s.Value.EnumerateArray().Select(x => x.GetInt32()).ToArray();
        return d;
    }

    // the DXCC entity of a callsign (its longest prefix), and that prefix
    public static (Entity entity, string prefix)? EntityOf(string call)
    {
        var d = _data.Value;
        call = call.Trim().ToUpperInvariant();
        for (int n = Math.Min(call.Length, d.MaxPrefix); n >= 1; n--)
            if (d.Prefixes.TryGetValue(call[..n], out var i)) return (d.Entities[i], call[..n]);
        return null;
    }

    // one-letter countries: the second letters in use when the file gives
    // only the letter (null: all)
    static readonly Dictionary<char, string?> OneLetter = new()
    {
        ['F'] = "",            // F5ZZO; FG, FM, FY... are entities of their own
        // UK: GB special events, X clubs, and the new O Q R V letters
        // (MQ0..., GR4..., in the AD1C file as exceptions); GM, GW, GI...
        // are entities of their own
        ['G'] = "BXOQRV",
        ['M'] = "XOQRV",
        ['I'] = null, ['K'] = null, ['N'] = null, ['W'] = null, ['R'] = null, ['U'] = null,
    };

    // null if the callsign looks real, else why not (shown, written to the CSV)
    public static string? Doubt(string call, string? locator)
    {
        call = call.Trim().ToUpperInvariant();
        if (call == "") return null;
        if (EntityOf(call) is not var (entity, prefix)) return L.T("préfixe inconnu");
        if (prefix.Length == 1 && call.Length > 1 && char.IsLetter(call[1])
            && OneLetter.TryGetValue(prefix[0], out var seconds) && seconds is not null && !seconds.Contains(call[1]))
            return L.F("préfixe {0} non utilisé par les radioamateurs", call[..2]);
        if (locator is { Length: >= 4 } loc && !Near(entity, loc.ToUpperInvariant()))
            return L.F("locator {0} loin du pays de l'indicatif ({1})", loc.ToUpperInvariant(), entity.Name);
        return null;
    }

    static bool Near(Entity e, string loc)
    {
        if (Maidenhead.Center(loc[..4]) is null) return true;           // not a locator: not judged here
        if (e.Country >= 0 && _data.Value.Squares.TryGetValue(loc[..4], out var cs) && cs.Contains(e.Country)) return true;
        var at = Maidenhead.Center(loc.Length >= 6 ? loc[..6] : loc[..4]) ?? Maidenhead.Center(loc[..4])!.Value;
        return Km(at.lat, at.lon, e.Lat, e.Lon) <= 500;
    }

    static double Km(double la1, double lo1, double la2, double lo2)
    {
        double d2r = Math.PI / 180;
        double c = Math.Sin(la1 * d2r) * Math.Sin(la2 * d2r) + Math.Cos(la1 * d2r) * Math.Cos(la2 * d2r) * Math.Cos((lo2 - lo1) * d2r);
        return 6371 * Math.Acos(Math.Clamp(c, -1, 1));
    }
}
