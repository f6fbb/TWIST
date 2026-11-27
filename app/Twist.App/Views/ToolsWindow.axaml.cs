using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Twist.App.ViewModels;

namespace Twist.App.Views;

public partial class ToolsWindow : Window
{
    public ToolsWindow()
    {
        InitializeComponent();
        Opened += (_, _) => { if (WavTab) Tabs.SelectedIndex = Tabs.ItemCount - 1; };
    }

    // opened on the "WAV de test" tab (Outils › WAV de test…)
    public bool WavTab { get; init; }

    ToolsViewModel? Vm => DataContext as ToolsViewModel;

    async void OnCopyBits(object? sender, RoutedEventArgs e)
    {
        if (Vm is not null && Clipboard is not null) await Clipboard.SetTextAsync(Vm.Bits);
    }

    async void OnCopyHex(object? sender, RoutedEventArgs e)
    {
        if (Vm is not null && Clipboard is not null) await Clipboard.SetTextAsync(Vm.Hex);
    }

    // WAV de test: where to write it, then the view model writes and checks it
    async void OnWriteWav(object? sender, RoutedEventArgs e)
    {
        if (Vm is null) return;
        var r = Vm.WavRequest();
        var name = $"twist-test-{(r.Proto == Twist.Native.TwlProto.Pi4 ? "pi4" : Engine.Label(r.Proto).Replace(" ", "").ToLowerInvariant())}-"
                   + $"{(r.Call == "" ? r.Loc : r.Call).Replace("/", "-")}-{(r.SnrDb is double d ? $"{d:0}dB" : "clean")}.wav";
        var file = await StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
        {
            Title = Twist.App.Lang.L.T("Enregistrer le WAV de test"),
            SuggestedFileName = name,
            DefaultExtension = "wav",
            FileTypeChoices = [new FilePickerFileType("WAV") { Patterns = ["*.wav"] }],
        });
        if (file?.TryGetLocalPath() is string path) await Vm.WavWrite(path);
    }
}
