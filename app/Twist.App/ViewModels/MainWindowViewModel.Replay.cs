using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Twist.App.Lang;

namespace Twist.App.ViewModels;

// The replay of a WAV file: a strip under the status strip while a file
// is the source - how far it is (bar, time, percent), the frames decoded
// in it, fast or real speed, stop, replay, back to the audio input.
public partial class MainWindowViewModel
{
    [ObservableProperty] private bool _replayShown;
    [ObservableProperty] private bool _replayPlaying;
    [ObservableProperty] private double _replayPercent;
    [ObservableProperty] private string _replayText = "";
    [ObservableProperty] private bool _replayFast;
    int _replaySpots;

    partial void OnReplayFastChanged(bool value) { if (_rx is not null) _rx.Fast = value; }

    // every 0.1 s (the window's fast timer)
    public void ReplayTick()
    {
        if (_rx is null || SelectedSource is not { Path: string path }) { ReplayShown = false; return; }
        ReplayShown = true;
        ReplayPlaying = _rx.Replaying;
        double total = _rx.FileSeconds, pos = _rx.FilePosition;
        ReplayPercent = total > 0 ? Math.Clamp(100 * pos / total, 0, 100) : 0;
        var name = Path.GetFileName(path);
        string where = $"{Clock(pos)} / {Clock(total)}";
        ReplayText = ReplayPlaying
            ? (pos >= total ? L.F("{0} · fin du fichier, dernières passes… · décodages : {1}", name, _replaySpots)
                            : L.F("{0} · {1} · {2:0} % · décodages : {3}", name, where, ReplayPercent, _replaySpots))
            : _rx.FileFinished ? L.F("{0} lu jusqu'au bout · décodages : {1}", name, _replaySpots)
                               : L.F("{0} · lecture arrêtée à {1} · décodages : {2}", name, where, _replaySpots);
    }

    static string Clock(double s) => TimeSpan.FromSeconds(Math.Floor(s)).ToString(s >= 3600 ? @"h\:mm\:ss" : @"m\:ss");

    [RelayCommand]
    void StopReplay()
    {
        _rx?.Stop();
        UpdateFooter();
    }

    [RelayCommand]
    void ReplayAgain()
    {
        if (SelectedSource is { Path: string path }) SelectFile(path);
    }

    // back to the sound card chosen in the settings (else the default one)
    [RelayCommand]
    void BackToInput()
    {
        var input = _cfg.Current.Audio.Input;
        var item = Sources.FirstOrDefault(x => !x.IsFile && x.Path is null && x.Name == input)
                   ?? Sources.FirstOrDefault(x => !x.IsFile && x.Path is null);
        if (item is null) { Log(L.T("aucune entrée audio trouvée"), warning: true); return; }
        DecodingActive = true;
        SelectedSource = item;
    }
}
