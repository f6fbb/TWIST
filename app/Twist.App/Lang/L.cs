using System.Globalization;
using System.Reflection;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;

namespace Twist.App.Lang;

// The interface language (Réglages › Interface). The program is written
// in French; for English every French text is looked up in en.json
// (French text -> English text, embedded in the program):
//   - texts of the code: L.T("Réception arrêtée"), L.F("passe toutes les {0} s", p)
//     (L.F: the French format string is the key, {0} {1}... its holes);
//   - texts of the windows (XAML): translated when each control is
//     loaded (Translate), only the literal French ones - a value coming
//     from a binding is the view model's, already translated.
// A text missing from en.json stays in French (and the test
// LanguageTests lists it). The language applies at the next start.
public static class L
{
    public static string Language { get; private set; } = "fr";
    static Dictionary<string, string> _en = new();

    public static bool English => Language == "en";

    public static void Set(string lang)
    {
        Language = lang == "en" ? "en" : "fr";
        if (English && _en.Count == 0) _en = Load();
    }

    public static Dictionary<string, string> Load()
    {
        using var s = Assembly.GetExecutingAssembly().GetManifestResourceStream("Twist.App.Lang.en.json");
        if (s is null) return new();
        return JsonSerializer.Deserialize<Dictionary<string, string>>(s, new JsonSerializerOptions { ReadCommentHandling = JsonCommentHandling.Skip })
               ?? new();
    }

    public static string T(string fr) =>
        English && _en.TryGetValue(fr, out var en) ? en : fr;

    public static string F(string frFormat, params object?[] args) =>
        string.Format(Culture, T(frFormat), args);

    // numbers as the language writes them: 144,489 000 / 144.489 000
    public static CultureInfo Culture => English ? CultureInfo.GetCultureInfo("en-GB") : CultureInfo.GetCultureInfo("fr-FR");
    public static string Dec(string s) => English ? s : s.Replace('.', ',');

    // ---- the windows ----

    static bool _hooked;

    // once, at start: every control is translated when it is loaded
    public static void HookControls()
    {
        if (_hooked || !English) return;
        _hooked = true;
        Control.LoadedEvent.AddClassHandler<Control>((c, _) => Translate(c), RoutingStrategies.Direct);
    }

    static void Translate(Control c)
    {
        // SetCurrentValue: the text shown changes, a binding stays in place
        void Str(AvaloniaProperty p)
        {
            if (c.GetValue(p) is string s && s != "" && _en.TryGetValue(s, out var en)) c.SetCurrentValue(p, en);
        }
        switch (c)
        {
            case TextBlock: Str(TextBlock.TextProperty); break;
            case TextBox: Str(TextBox.WatermarkProperty); break;
            case ComboBox: Str(ComboBox.PlaceholderTextProperty); break;
            case MenuItem: Str(MenuItem.HeaderProperty); break;
            case TabItem: Str(TabItem.HeaderProperty); break;
            case Window: Str(Window.TitleProperty); break;
            case DataGrid g:
                foreach (var col in g.Columns)
                    if (col.Header is string h && _en.TryGetValue(h, out var eh)) col.Header = eh;
                break;
        }
        if (c is ContentControl and not Window) Str(ContentControl.ContentProperty);
        if (ToolTip.GetTip(c) is string tip && _en.TryGetValue(tip, out var et)) ToolTip.SetTip(c, et);
    }
}
