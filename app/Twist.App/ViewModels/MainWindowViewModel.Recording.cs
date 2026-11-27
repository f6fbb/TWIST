using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Twist.App.Lang;
using Twist.App.Services;

namespace Twist.App.ViewModels;

// The recordings (Services/Recorder.cs), at about 12 kHz, in
// <journal folder>/enregistrements/:
// - Réception › Garder les 2 dernières minutes (Ctrl+S): what the
//   capture holds, written at once - for something seen on the waterfall
//   that was not decoded (aircraft scatter...);
// - Réception › Enregistrer en continu: from now on, until stopped, the
//   duration limit, or the disk's free space limit (Réglages ›
//   Rapports); a red badge beside "Décodage actif" while it runs (a
//   click stops it). Live sources only.
public partial class MainWindowViewModel
{
    public const double LastSeconds = 120;
    Recorder? _recorder;

    [ObservableProperty] private bool _recording;
    [ObservableProperty] private string _recordingText = "";
    public string RecordMenuText => Recording ? L.T("Arrêter l'enregistrement continu") : L.T("Enregistrer en continu");
    partial void OnRecordingChanged(bool value) => OnPropertyChanged(nameof(RecordMenuText));

    string Source => _rx?.SourceName ?? "";
    double? RadioMHz => Volatile.Read(ref _radioMHz) is var m && m > 0 ? m : null;

    [RelayCommand]
    async Task SaveLast()
    {
        if (_rx?.TakeLast(LastSeconds) is not { } a)
        {
            Log(L.T("enregistrement : pas d'audio (réception arrêtée)"), warning: true);
            return;
        }
        string folder = _cfg.Reports.LogFolder, app = Report.AppVersion, src = Source;
        double? mhz = RadioMHz;
        try
        {
            var path = await Task.Run(() => Recordings.SaveLast(folder, a.x, a.fs, a.t0, mhz, app, src));
            Log(L.F("{0} s gardées : {1}", Math.Round(a.x.Length / (double)a.fs), Path.GetFileName(path)));
            LastRecording = path;
        }
        catch (Exception e) { Log(L.T("enregistrement : ") + e.Message, warning: true); }
    }
    // (the screenshots and tests: the file just written)
    public string? LastRecording { get; private set; }

    [RelayCommand]
    void ToggleRecording()
    {
        if (_recorder is not null) { StopRecording(L.T("arrêté")); return; }
        if (_rx is null || !_rx.Live)
        {
            Log(L.T("enregistrement continu : seulement depuis une entrée audio (pas pendant la relecture d'un fichier)"), warning: true);
            return;
        }
        if (FreeGB() is double free && free < _cfg.Reports.RecordMinFreeGB)
        {
            Log(L.F("enregistrement continu : {0:0.0} Go libres, moins que la limite ({1:0.#} Go)", free, _cfg.Reports.RecordMinFreeGB), warning: true);
            return;
        }
        try
        {
            var st = _rx.Stats;
            var start = DateTime.UtcNow.AddSeconds(-(st.LatencyS + _rx.ChainDelaySeconds));
            _recorder = new Recorder(_cfg.Reports.LogFolder, st.Rate, start, RadioMHz, Report.AppVersion, Source);
            _rx.StartRecording(_recorder);
            Recording = true;
            LastRecording = _recorder.Path;
            Log(L.F("enregistrement continu : {0}", Path.GetFileName(_recorder.Path)));
            RecordTick();
        }
        catch (Exception e)
        {
            _recorder = null;
            Log(L.T("enregistrement : ") + e.Message, warning: true);
        }
    }

    void StopRecording(string why)
    {
        var r = _recorder;
        if (r is null) return;
        _rx?.StopRecording();
        _recorder = null;
        Recording = false;
        try { r.Dispose(); }
        catch (Exception e) { Log(L.T("enregistrement : ") + e.Message, warning: true); }
        Log(L.F("enregistrement continu {0} : {1}, {2}, {3:0.0} Mo", why, Path.GetFileName(r.Path), Duration(r.Seconds), r.Bytes / 1048576.0)
            + (r.Gaps > 0 ? L.F(" ({0} interruptions)", r.Gaps) : ""));
    }

    [RelayCommand]
    void OpenRecordingsFolder()
    {
        Directory.CreateDirectory(Recordings.Folder(_cfg.Reports.LogFolder));
        OpenInShell(Recordings.Folder(_cfg.Reports.LogFolder), folder: true);
    }

    // every second: the badge, and the limits
    void RecordTick()
    {
        var r = _recorder;
        if (r is null) return;
        if (_rx is null || !_rx.Recording) { StopRecording(L.T("arrêté (source changée)")); return; }
        int max = _cfg.Reports.RecordMaxMinutes;
        if (max > 0 && r.Seconds >= max * 60) { StopRecording(L.F("arrêté au bout de {0} min", max)); return; }
        if (FreeGB() is double free && free < _cfg.Reports.RecordMinFreeGB)
        {
            StopRecording(L.F("arrêté : moins de {0:0.#} Go libres sur le disque", _cfg.Reports.RecordMinFreeGB));
            return;
        }
        RecordingText = $"● REC {Duration(r.Seconds)}";
    }

    static string Duration(double s) =>
        s >= 3600 ? $"{(int)(s / 3600)}:{(int)(s % 3600 / 60):00}:{(int)(s % 60):00}" : $"{(int)(s / 60)}:{(int)(s % 60):00}";

    // free space of the disk of the journal folder, GB (null: unknown)
    double? FreeGB()
    {
        try
        {
            var root = Path.GetPathRoot(Path.GetFullPath(_cfg.Reports.LogFolder));
            return root is null ? null : new DriveInfo(root).AvailableFreeSpace / 1e9;
        }
        catch { return null; }
    }
}
