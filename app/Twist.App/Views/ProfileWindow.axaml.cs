using Avalonia.Controls;
using Avalonia.Interactivity;
using Twist.App.Lang;

namespace Twist.App.Views;

// The small dialogs of the profiles: a new one (name, copy of the current
// one or the defaults), rename (name only), delete (a question); and a
// plain message.
// Check: the action itself, called on OK; its message keeps the dialog
// open with the reason.
public partial class ProfileWindow : Window
{
    Func<string, bool, string?>? _check;

    public ProfileWindow() => InitializeComponent();

    public static ProfileWindow New(string current, Func<string, bool, string?> create)
    {
        var w = new ProfileWindow { Title = L.T("Nouveau profil"), _check = create };
        w.Heading.Text = L.T("Nouveau profil");
        w.FromCopy.Content = L.F("une copie de « {0} » (carte son, radio, décodage, balise)", current);
        w.OkButton.Content = L.T("Créer");
        w.Opened += (_, _) => w.NameBox.Focus();
        return w;
    }

    public static ProfileWindow Rename(string current, Func<string, string?> rename)
    {
        var w = new ProfileWindow { Title = L.T("Renommer le profil"), _check = (n, _) => rename(n) };
        w.Heading.Text = L.F("Renommer « {0} »", current);
        w.NameBox.Text = current;
        w.FromPanel.IsVisible = false;
        w.OkButton.Content = L.T("Renommer");
        w.Opened += (_, _) => { w.NameBox.Focus(); w.NameBox.SelectAll(); };
        return w;
    }

    public static ProfileWindow Confirm(string question, string ok)
    {
        var w = new ProfileWindow { Title = L.T("Profil"), _check = (_, _) => null };
        w.Heading.Text = question;
        w.NamePanel.IsVisible = false;
        w.FromPanel.IsVisible = false;
        w.OkButton.Content = ok;
        return w;
    }

    // a message with OK only (used outside the profiles too); with
    // `cancel`, two buttons (ShowDialog<bool> tells which)
    public static ProfileWindow Message(string title, string heading, string body, string ok = "OK", string? cancel = null)
    {
        var w = new ProfileWindow { Title = title, _check = (_, _) => null };
        w.Heading.Text = heading;
        w.Body.Text = body;
        w.Body.IsVisible = body != "";
        w.NamePanel.IsVisible = false;
        w.FromPanel.IsVisible = false;
        w.CancelButton.IsVisible = cancel is not null;
        if (cancel is not null) w.CancelButton.Content = cancel;
        w.OkButton.Content = ok;
        return w;
    }

    void OnOk(object? sender, RoutedEventArgs e)
    {
        var why = _check?.Invoke(NameBox.Text ?? "", FromCopy.IsChecked == true);
        if (why is null) { Close(true); return; }
        ErrorText.Text = why;
        ErrorText.IsVisible = true;
    }

    void OnCancel(object? sender, RoutedEventArgs e) => Close(false);
}
