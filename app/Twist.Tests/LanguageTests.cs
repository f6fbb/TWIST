using System.Net;
using System.Text.RegularExpressions;
using Twist.App.Lang;
using Xunit;

namespace Twist.Tests;

// Switching the language is global: these tests run alone.
[CollectionDefinition("language", DisableParallelization = true)]
public class LanguageCollection { }

// Every French text of the program has its English text in
// Lang/en.json, with the same {0} {1}... holes.
[Collection("language")]
public class LanguageTests
{
    static string Root()
    {
        var d = new DirectoryInfo(AppContext.BaseDirectory);
        while (d is not null && !Directory.Exists(Path.Combine(d.FullName, "app", "Twist.App"))) d = d.Parent;
        return d?.FullName ?? throw new DirectoryNotFoundException("repository root");
    }

    static IEnumerable<string> Keys()
    {
        var app = Path.Combine(Root(), "app", "Twist.App");
        foreach (var f in Directory.GetFiles(app, "*.cs", SearchOption.AllDirectories))
        {
            if (f.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}")) continue;
            foreach (Match m in Regex.Matches(File.ReadAllText(f), @"L\.[TF]\(""((?:[^""\\]|\\.)*)"""))
                yield return Regex.Unescape(m.Groups[1].Value);
        }
        foreach (var f in Directory.GetFiles(Path.Combine(app, "Views"), "*.axaml"))
        {
            var s = File.ReadAllText(f);
            foreach (Match m in Regex.Matches(s, @"\b(?:Text|Content|Header|ToolTip\.Tip|Watermark|Title|PlaceholderText)=""([^""{][^""]*)"""))
                yield return WebUtility.HtmlDecode(m.Groups[1].Value);
            foreach (Match m in Regex.Matches(s, @">([^<>{}]*[A-Za-zÀ-ÿ][^<>{}]*)</(?:Button|ToggleButton|CheckBox|RadioButton|TextBlock|MenuItem|TabItem)>"))
                yield return WebUtility.HtmlDecode(m.Groups[1].Value).Trim();
        }
    }

    static IEnumerable<string> Holes(string s) => Regex.Matches(s, @"\{(\d+)[^}]*\}").Select(m => m.Groups[1].Value).OrderBy(x => x);

    [Fact]
    public void Every_text_has_its_English_text()
    {
        var en = L.Load();
        var missing = Keys().Where(k => Regex.IsMatch(k, @"[A-Za-zÀ-ÿ]") && !en.ContainsKey(k)).Distinct().ToList();
        Assert.True(missing.Count == 0, "missing in en.json:\n" + string.Join("\n", missing));
    }

    [Fact]
    public void Same_holes_in_both_languages()
    {
        var bad = L.Load().Where(p => !Holes(p.Key).SequenceEqual(Holes(p.Value))).Select(p => p.Key).ToList();
        Assert.True(bad.Count == 0, "holes differ:\n" + string.Join("\n", bad));
    }

    [Fact]
    public void Formats_in_English_and_French()
    {
        L.Set("fr");
        Assert.Equal("passe toutes les 3 s", L.F("passe toutes les {0} s", 3));
        L.Set("en");
        try
        {
            Assert.Equal("a pass every 3 s", L.F("passe toutes les {0} s", 3));
            Assert.Equal("Cancel", L.T("Annuler"));
            Assert.Equal("texte inconnu", L.T("texte inconnu"));       // missing: stays French
            Assert.Equal("1.5", L.Dec("1.5"));
        }
        finally { L.Set("fr"); }
        Assert.Equal("1,5", L.Dec("1.5"));
    }
}
