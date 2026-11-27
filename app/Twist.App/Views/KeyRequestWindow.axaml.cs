using Avalonia.Controls;
using Avalonia.Interactivity;
using Twist.App.Lang;
using Twist.App.ViewModels;

namespace Twist.App.Views;

// Réglages › Rapports › Demander une clé…: the callsign and locator of
// the Station tab, an email address; the answer of fbbspots shown here.
public partial class KeyRequestWindow : Window
{
    readonly SettingsViewModel? _vm;

    public KeyRequestWindow() => InitializeComponent();

    public KeyRequestWindow(SettingsViewModel vm) : this()
    {
        _vm = vm;
        var call = vm.Callsign.Trim().ToUpperInvariant();
        var loc = vm.Locator.Trim().ToUpperInvariant();
        bool ready = call != "" && Services.LocatorBook.IsLocator(loc);
        Station.Text = ready ? L.F("Station {0} · {1}", call, loc) : L.T("Indicatif et locator à renseigner d'abord (onglet Station).");
        Email.IsEnabled = AskButton.IsEnabled = ready;
        Opened += (_, _) => Email.Focus();
    }

    async void OnAsk(object? sender, RoutedEventArgs e)
    {
        if (_vm is null) return;
        var email = Email.Text?.Trim() ?? "";
        if (!email.Contains('@')) { ShowAnswer(L.T("Adresse email à renseigner."), false); return; }
        AskButton.IsEnabled = false;
        try
        {
            ShowAnswer(L.T("demande en cours…"), true);
            var (ok, msg) = await _vm.RequestKey(email);
            ShowAnswer(msg + (ok ? L.T(" Collez ensuite la clé reçue dans « Clé fbbspots de la station ».") : ""), ok);
            AskButton.IsEnabled = !ok;
        }
        catch (Exception ex)            // (an async void: an error here would end the program)
        {
            Program.Log(ex, "key request");
            ShowAnswer(ex.Message, false);
            AskButton.IsEnabled = true;
        }
    }

    // (public: the screenshot tool shows an answer)
    public void ShowAnswer(string text, bool ok)
    {
        Result.Text = text;
        Result.Classes.Set("error", !ok);
        Result.IsVisible = true;
    }

    void OnClose(object? sender, RoutedEventArgs e) => Close();
}
