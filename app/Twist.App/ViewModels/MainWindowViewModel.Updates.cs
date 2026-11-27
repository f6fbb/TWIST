using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Twist.App.Lang;
using Twist.App.Services;

namespace Twist.App.ViewModels;

// A new version (Services/Updates): a window (Views/UpdateWindow) - what
// it is, its changes, Installer (Windows: downloaded, checked, then the
// installer closes TWIST Console and starts it again), Plus tard - and a
// banner above the spots as a reminder (its own Plus tard: until the next
// start). Looked for 20 s after the start, then once a day, if allowed
// (Réglages › Interface): the window once per version and start; Aide ›
// Rechercher une mise à jour asks at once and always answers in a window
// (up to date, or the server's error).
public partial class MainWindowViewModel
{
    readonly Updates _updates = new();
    public Updates UpdateService => _updates;          // (the screenshots: a fake server)
    DispatcherTimer? _updateTimer;
    UpdateInfo? _update;
    CancellationTokenSource? _updateCts;

    [ObservableProperty] private bool _updateBanner;
    [ObservableProperty] private string _updateText = "";
    [ObservableProperty] private bool _updateBusy;
    public bool UpdateCanInstall => OperatingSystem.IsWindows();
    // (elsewhere: its page, to download it)
    public string UpdateButtonText => UpdateCanInstall ? L.T("Installer") : L.T("Télécharger");
    // its changes (the CHANGELOG section, its Markdown lightened)
    // (the CHANGELOG is in English: its headings at least in the language of the interface)
    public string UpdateNotes => _update is null ? "" : string.Join("\n", _update.Notes.Replace("\r", "").Split('\n')
        .Select(l => l.StartsWith("### ") ? Heading(l[4..].Trim()) : l.StartsWith("- ") ? "• " + l[2..] : l));
    static string Heading(string h) => h switch
    {
        "Added" => L.T("Ajouts"),
        "Changed" => L.T("Changements"),
        "Fixed" => L.T("Corrections"),
        "Removed" => L.T("Retraits"),
        "Tests" => L.T("Essais"),
        _ => h,
    };
    public string UpdateTitle => _update is null ? "" : L.F("TWIST Console {0}", _update.Version) + (_update.Date == "" ? "" : $" · {_update.Date}");

    // the view: close the program, the installer is running
    public event Action? QuitForUpdate;
    // the view: the window of a new version (true), or the answer to a
    // check asked for (false, its text)
    public event Action<bool, string?>? UpdateDialogRequested;
    string? _prompted;                                   // the version its window was shown for

    void StartUpdateChecks()
    {
        _updateTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(20) };
        _updateTimer.Tick += async (_, _) =>
        {
            _updateTimer.Interval = TimeSpan.FromHours(24);
            if (_cfg.Interface.CheckUpdates && !UpdateBanner) await CheckUpdate(manual: false);
        };
        _updateTimer.Start();
    }

    [RelayCommand]
    public async Task CheckUpdateNow() => await CheckUpdate(manual: true);

    async Task CheckUpdate(bool manual)
    {
        _updates.Url = _cfg.Interface.UpdateUrl is { Length: > 0 } u ? u : Updates.DefaultUrl;
        try
        {
            var i = await _updates.CheckAsync();
            if (i is null || !Updates.Newer(i.Version, Report.Version))
            {
                if (manual)
                {
                    Log(L.F("mise à jour : TWIST Console {0} est la dernière version", Report.Version));
                    UpdateDialogRequested?.Invoke(false, L.F("Vous avez la dernière version, TWIST Console {0}.", Report.Version));
                }
                return;
            }
            _update = i;
            UpdateText = L.F("TWIST Console {0} est disponible (vous avez la {1}).", i.Version, Report.Version);
            OnPropertyChanged(nameof(UpdateNotes));
            OnPropertyChanged(nameof(UpdateTitle));
            UpdateBanner = true;
            Log(L.F("mise à jour : TWIST Console {0} disponible", i.Version));
            if (manual || _prompted != i.Version)
            {
                _prompted = i.Version;
                UpdateDialogRequested?.Invoke(true, null);
            }
        }
        catch (Exception e)
        {
            // (by itself: quietly, the next try is tomorrow)
            if (manual)
            {
                Log(L.T("mise à jour : serveur injoignable — ") + e.Message, warning: true);
                UpdateDialogRequested?.Invoke(false, L.T("Le serveur des mises à jour ne répond pas : ") + e.Message);
            }
        }
    }

    [RelayCommand] void UpdateNotesShow() => UpdateDialogRequested?.Invoke(true, null);

    [RelayCommand]
    void UpdateLater()
    {
        _updateCts?.Cancel();
        UpdateBanner = false;
    }

    [RelayCommand]
    async Task InstallUpdate()
    {
        if (_update is null || UpdateBusy) return;
        if (!UpdateCanInstall)
        {
            OpenUpdatePage();
            return;
        }
        // not while transmitting: the installer closes the program
        if (TxActive || CycleRunning)
        {
            UpdateText = L.T("Arrêter d'abord l'émission et le cycle de balise, puis Installer.");
            return;
        }
        var f = Updates.Choose(_update, Report.Version);
        if (f is null) { OpenUpdatePage(); return; }
        UpdateBusy = true;
        _updateCts = new CancellationTokenSource();
        try
        {
            var progress = new Progress<double>(p => UpdateText = L.F("Téléchargement de TWIST Console {0} : {1:0} %", _update.Version, 100 * p));
            var path = await _updates.DownloadAsync(f, Path.Combine(Path.GetTempPath(), "TWIST-Console-update"), progress, _updateCts.Token);
            UpdateText = L.T("Fichier vérifié : l'installation commence, TWIST Console va se fermer puis se relancer.");
            Log(L.F("mise à jour : {0} vérifié, installation", f.File));
            SaveNow();
            Updates.Launch(path);
            QuitForUpdate?.Invoke();
        }
        catch (OperationCanceledException) { UpdateText = L.T("Téléchargement annulé."); }
        catch (Exception e)
        {
            UpdateText = L.T("Mise à jour impossible : ") + e.Message;
            Log(L.T("mise à jour impossible : ") + e.Message, warning: true);
        }
        finally { UpdateBusy = false; }
    }

    // where to download it by hand (Mac, Linux, or no installer listed)
    void OpenUpdatePage()
    {
        var page = _update?.Page is { Length: > 0 } p ? p : new Uri(new Uri(_updates.Url), ".").ToString();
        try
        {
            var psi = OperatingSystem.IsLinux() ? new System.Diagnostics.ProcessStartInfo("xdg-open")
                                                : new System.Diagnostics.ProcessStartInfo(page) { UseShellExecute = true };
            if (OperatingSystem.IsLinux()) psi.ArgumentList.Add(page);
            System.Diagnostics.Process.Start(psi);
        }
        catch (Exception e) { Log(L.T("ouverture de la page : ") + e.Message, warning: true); }
    }
}
