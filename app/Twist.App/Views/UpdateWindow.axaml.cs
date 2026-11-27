using Avalonia.Controls;
using Avalonia.Interactivity;
using Twist.App.Lang;
using Twist.App.ViewModels;

namespace Twist.App.Views;

// A new version: what it is, its changes, Installer / Plus tard (the
// banner of the main window stays as a reminder). Or, without one, the
// answer to Aide › Rechercher une mise à jour: `message`, and OK.
public partial class UpdateWindow : Window
{
    public UpdateWindow() => InitializeComponent();

    public UpdateWindow(MainWindowViewModel vm, bool available, string? message) : this()
    {
        DataContext = vm;
        Head.Text = available ? L.T("Une nouvelle version est disponible") : L.T("Mise à jour");
        if (!available) Body.Text = message ?? "";              // (in place of the binding)
        NotesBox.IsVisible = available && vm.UpdateNotes != "";
        InstallButton.IsVisible = LaterButton.IsVisible = available;
        OkButton.IsVisible = !available;
    }

    void OnClose(object? sender, RoutedEventArgs e) => Close();
}
