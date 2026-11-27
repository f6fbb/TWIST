using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Twist.App.ViewModels;
using Twist.App.Lang;

namespace Twist.App.Views;

// Closes with the new settings (Enregistrer, every field valid) or null.
// Appliquer: the settings saved and applied (Apply), the window kept
// open - to try another audio input, say; Annuler then closes without
// undoing what was applied.
public partial class SettingsWindow : Window
{
    public SettingsWindow() => InitializeComponent();

    SettingsViewModel? Vm => DataContext as SettingsViewModel;

    // given by the main window: applies and saves the settings
    public Action<Services.Settings>? Apply { get; set; }

    void OnCancel(object? sender, RoutedEventArgs e) => Close(null);

    void OnApply(object? sender, RoutedEventArgs e)
    {
        if (Vm?.TryBuild() is { } s) { Apply?.Invoke(s); Vm.Error = ""; }   // else: the error shown
    }

    void OnSave(object? sender, RoutedEventArgs e)
    {
        if (Vm?.TryBuild() is { } s) Close(s);       // else: the error is shown, the tab selected
    }

    async void OnNewProfile(object? sender, RoutedEventArgs e)
    {
        if (Vm is null) return;
        await ProfileWindow.New(Vm.CurrentProfileName, Vm.NewProfile).ShowDialog<bool>(this);
    }

    async void OnRenameProfile(object? sender, RoutedEventArgs e)
    {
        if (Vm is null) return;
        await ProfileWindow.Rename(Vm.CurrentProfileName, Vm.RenameProfile).ShowDialog<bool>(this);
    }

    async void OnDeleteProfile(object? sender, RoutedEventArgs e)
    {
        if (Vm is null) return;
        var ok = await ProfileWindow.Confirm(L.F("Supprimer le profil « {0} » ?", Vm.CurrentProfileName), L.T("Supprimer")).ShowDialog<bool>(this);
        if (ok) Vm.DeleteProfile();
    }

    async void OnRequestKey(object? sender, RoutedEventArgs e)
    {
        if (Vm is null) return;
        await new KeyRequestWindow(Vm).ShowDialog(this);
    }

    async void OnBrowse(object? sender, RoutedEventArgs e)
    {
        if (Vm is null) return;
        var start = await StorageProvider.TryGetFolderFromPathAsync(Vm.LogFolder);
        var r = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = L.T("Dossier du journal"),
            SuggestedStartLocation = start,
        });
        if (r.Count > 0 && r[0].TryGetLocalPath() is string path) Vm.LogFolder = path;
    }
}
