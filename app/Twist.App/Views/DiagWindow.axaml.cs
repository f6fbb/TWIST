using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Twist.App.Lang;
using Twist.App.Services;

namespace Twist.App.Views;

// The fbbspots diagnostic (Services/FbbspotsDiag): run when the window
// opens and on Relancer; Copier puts the whole text in the clipboard, to
// paste in a message to the administrator.
public partial class DiagWindow : Window
{
    readonly Func<Task<List<DiagLine>>>? _run;
    List<DiagLine> _lines = new();

    public DiagWindow() => InitializeComponent();

    public DiagWindow(Func<Task<List<DiagLine>>> run) : this()
    {
        _run = run;
        Opened += async (_, _) => await Run();
    }

    // (the screenshots: the lines given)
    public bool Busy { get; private set; }

    async Task Run()
    {
        if (_run is null) return;
        Busy = true;
        AgainButton.IsEnabled = CopyButton.IsEnabled = false;
        Summary.Text = L.T("vérification en cours…");
        Lines.ItemsSource = null;
        try { Show(await _run()); }
        catch (Exception e) { Show([new DiagLine(DiagState.Fail, L.T("Diagnostic"), e.Message)]); }
        finally { AgainButton.IsEnabled = CopyButton.IsEnabled = true; Busy = false; }
    }

    public void Show(List<DiagLine> lines)
    {
        _lines = lines;
        int fail = lines.Count(l => l.State == DiagState.Fail), warn = lines.Count(l => l.State == DiagState.Warn);
        Summary.Text = fail > 0 ? L.F("{0} problème(s) trouvé(s) : voir les lignes ✘", fail)
                     : warn > 0 ? L.F("rien de bloquant, {0} point(s) à surveiller (⚠)", warn)
                     : L.T("tout est bon : les spots peuvent partir");
        Lines.ItemsSource = lines.Select(Row).ToList();
    }

    static Control Row(DiagLine l)
    {
        var color = l.State switch
        {
            DiagState.Ok => "#5FBF77", DiagState.Warn => "#E5B54C", DiagState.Fail => "#E5645C", _ => "#93A1AF",
        };
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("24,120,*") };
        var mark = new TextBlock { Text = l.Mark, Foreground = new SolidColorBrush(Color.Parse(color)), FontWeight = FontWeight.Bold };
        var title = new TextBlock { Text = l.Title, FontWeight = FontWeight.SemiBold };
        var detail = new SelectableTextBlock { Text = l.Detail, TextWrapping = TextWrapping.Wrap };
        Grid.SetColumn(title, 1);
        Grid.SetColumn(detail, 2);
        g.Children.Add(mark); g.Children.Add(title); g.Children.Add(detail);
        return g;
    }

    async void OnCopy(object? sender, RoutedEventArgs e)
    {
        if (Clipboard is null) return;
        var text = $"{Report.AppVersion} · {DateTime.UtcNow:yyyy-MM-dd HH:mm:ss} UTC\n" + string.Join("\n", _lines.Select(l => l.ToString()));
        await Clipboard.SetTextAsync(text);
    }

    async void OnAgain(object? sender, RoutedEventArgs e) => await Run();
    void OnClose(object? sender, RoutedEventArgs e) => Close();
}
