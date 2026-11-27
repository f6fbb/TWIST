using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Globalization;
using Avalonia.Media;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Twist.App.Services;
using Twist.Native;
using Twist.App.Lang;

namespace Twist.App.ViewModels;

// One line of the spots table.
public sealed partial class SpotViewModel : ObservableObject
{
    public required string Start { get; init; }     // UTC, start of the frame
    public required TwlProto Proto { get; init; }
    public string ProtoText => Engine.Label(Proto) + (Deep ? " *" : "");
    public bool IsClassic => Proto == TwlProto.Classic;      // colour: TextBlock.proto.classic (App.axaml)
    public bool IsPi4 => Proto == TwlProto.Pi4;              // colour: TextBlock.proto.pi4
    public bool Deep { get; init; }
    // (a PI4 locator line: its callsign can be given later, CallGuessed)
    [ObservableProperty, NotifyPropertyChangedFor(nameof(CallText))] private string _call = "";
    // a callsign that may be a ghost (Services/CallCheck): the line greyed,
    // "?" after the callsign, the reason in its tooltip
    public string? Doubt { get; init; }
    public bool Doubtful => Doubt is not null;
    public string CallText => Doubt is null ? Call : Call + " ?";
    public string? CallTip => Doubt is not null ? L.F("Indicatif douteux : {0}. Pas envoyé à fbbspots ni en MQTT ; clic droit › Supprimer la ligne.", Doubt)
                            : CallGuessed ? LocTip : null;
    // a callsign not received in the frame (a PI4 "/locator" message: the
    // callsign of the PI4 frame of the same beacon, the minute before or
    // after), in small letters, greyed and in italics like a locator found
    // elsewhere; the tooltip says where from
    [ObservableProperty, NotifyPropertyChangedFor(nameof(CallTip))] private bool _callGuessed;
    partial void OnLocTipChanged(string? value) => OnPropertyChanged(nameof(CallTip));
    // the locator: received in the frame, or found (LocGuessed, shown
    // greyed, LocTip says where from), with the distance and azimuth
    [ObservableProperty] private string _loc = "—";
    [ObservableProperty] private bool _locGuessed;
    [ObservableProperty] private string? _locTip;
    [ObservableProperty] private string _distance = "—";
    [ObservableProperty] private string _azimuth = "—";
    public required string Snr { get; init; }
    public required string Audio { get; init; }
    public required string Rf { get; init; }
}

// An entry of the Source list: an input device, or a WAV file to replay.
public sealed record SourceItem(string Name, int Device, bool IsFile = false, string? Path = null)
{
    public override string ToString() => Name;
}

// The main window, connected to the receiver (Services/Receiver.cs):
// source, protocols and search zone; spots and their CSV journal; status
// strip; waterfall; journal. Its values come from the settings
// (Services/Settings.cs) and every change made here goes back to them,
// saved two seconds later (and when the program ends).
// Transmission: MainWindowViewModel.Tx.cs. Still to come: radio (step 6).
public partial class MainWindowViewModel : ObservableObject, IDisposable
{
    static CultureInfo Fr => L.Culture;
    static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

    Settings _cfg;
    public Settings Config => _cfg;
    public string ConfigPath { get; }
    readonly CsvLog _csv;
    readonly DispatcherTimer _saveTimer = new() { Interval = TimeSpan.FromSeconds(2) };
    bool _loading = true;                 // filling the fields from the settings: no write-back

    // station
    public string Callsign => _cfg.Station.Callsign;
    public string Locator => _cfg.Station.Locator;
    public string ProfileText => Callsign == "" ? L.T("indicatif à régler (Fichier › Réglages)")
                                                : Locator == "" ? Callsign : $"{Callsign} · {Locator}";

    readonly Receiver? _rx;

    public ObservableCollection<SourceItem> Sources { get; } = new();
    [ObservableProperty] private SourceItem? _selectedSource;
    [ObservableProperty] private bool _decodingActive = true;

    // the window gives the file picker (it owns the storage provider)
    public Func<Task<string?>>? PickWavFile { get; set; }

    public string ClassicLabel => Engine.Label(TwlProto.Classic);
    // the optional protocols: shown only when their library is installed
    public bool HasClassic => Engine.HasClassic;
    public bool HasPi4 => Engine.HasPi4;
    [ObservableProperty] private bool _searchClassic = true;
    [ObservableProperty] private bool _searchTwistS = true;
    [ObservableProperty] private bool _searchTwistL = true;
    [ObservableProperty] private bool _searchPi4;
    [ObservableProperty] private string _centerHz = "1500";
    [ObservableProperty] private string _widthHz = "200";
    [ObservableProperty] private string _radioFrequency = "144,489 000";
    [ObservableProperty] private string _footerCsv = "";

    // status strip
    [ObservableProperty] private double _levelDbfs = -120;
    [ObservableProperty] private double _peakDbfs = -120;
    public string LevelText => LevelDbfs <= -119 ? "— dBFS"
        : $"{(LevelDbfs < 0 ? "−" : "")}{Math.Abs(LevelDbfs):0} dBFS · {Controls.LevelMeter.ZoneName(LevelDbfs)}";
    public IBrush LevelBrush => new SolidColorBrush(Controls.LevelMeter.ZoneColor(LevelDbfs));
    partial void OnLevelDbfsChanged(double value)
    {
        OnPropertyChanged(nameof(LevelText));
        OnPropertyChanged(nameof(LevelBrush));
    }
    [ObservableProperty] private string _utc = "";
    [ObservableProperty] private string _passText = "—";
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CpuTip))]
    private string _cpuText = "—";
    public string CpuTip => L.F("Processeur utilisé par le programme : {0} (tous cœurs confondus)", CpuText);
    [ObservableProperty] private string _audioText = "—";
    [ObservableProperty] private string _nextTx = L.T("émission : à venir");
    // "prochaine émission 14:15:00 (dans 7 min 12 s)": only while the cycle
    // runs and nothing is being sent (the badge says the rest)
    [ObservableProperty] private bool _nextTxShown;
    [ObservableProperty] private string _footerSource = "";
    [ObservableProperty] private string _footerZone = "";

    // waterfall and journal panels: open or folded; each of the three
    // parts (waterfall, spots, journal) can also be in a window of its own,
    // always open there
    // the source, the search, the passes and the audio, shown under the
    // essentials (Affichage › Informations techniques)
    [ObservableProperty] private bool _showTech;
    partial void OnShowTechChanged(bool value) => Touch();
    [ObservableProperty, NotifyPropertyChangedFor(nameof(WaterfallShown))] private bool _waterfallOpen = true;
    [ObservableProperty, NotifyPropertyChangedFor(nameof(JournalShown))] private bool _journalOpen = true;
    [ObservableProperty, NotifyPropertyChangedFor(nameof(WaterfallShown), nameof(WaterfallDetachTip))] private bool _waterfallDetached;
    [ObservableProperty, NotifyPropertyChangedFor(nameof(SpotsDetachTip))] private bool _spotsDetached;
    [ObservableProperty, NotifyPropertyChangedFor(nameof(JournalShown), nameof(JournalDetachTip))] private bool _journalDetached;
    public bool WaterfallShown => WaterfallOpen || WaterfallDetached;
    public bool JournalShown => JournalOpen || JournalDetached;
    public string WaterfallDetachTip => DetachTip(WaterfallDetached);
    public string SpotsDetachTip => DetachTip(SpotsDetached);
    public string JournalDetachTip => DetachTip(JournalDetached);
    // the check marks of the menus
    [RelayCommand]
    void Toggle(string what)
    {
        if (what == "decoding") DecodingActive = !DecodingActive;
        else if (what == "waterfall") WaterfallOpen = !WaterfallOpen;
        else if (what == "journal") JournalOpen = !JournalOpen;
        else if (what == "classic") SearchClassic = !SearchClassic;
        else if (what == "twistS") SearchTwistS = !SearchTwistS;
        else if (what == "twistL") SearchTwistL = !SearchTwistL;
        else if (what == "pi4") SearchPi4 = !SearchPi4;
        else if (what == "tech") ShowTech = !ShowTech;
    }

    [RelayCommand]
    void ToggleDetach(string part)
    {
        if (part == "waterfall") WaterfallDetached = !WaterfallDetached;
        else if (part == "spots") SpotsDetached = !SpotsDetached;
        else if (part == "journal") JournalDetached = !JournalDetached;
    }
    static string DetachTip(bool detached) =>
        detached ? L.T("Remettre dans la fenêtre principale") : L.T("Détacher dans une fenêtre séparée (un autre écran, par exemple)");
    public Controls.IAudioTap? Tap => _rx;
    public ObservableCollection<Controls.WaterfallMark> Marks { get; } = new();
    [ObservableProperty] private double _wfCenter = 1500;
    [ObservableProperty] private double _cpuPercent;          // the gauge under the input level
    [ObservableProperty] private double _wfHalfWidth = 200;
    // the zones shown: the OOK frames' when one is searched, PI4's when it is
    [ObservableProperty] private bool _wfShowZone = true;
    [ObservableProperty] private bool _wfShowPi4;
    [ObservableProperty] private double _wfPi4Center = 800;
    [ObservableProperty] private double _wfPi4HalfWidth = 150;
    [ObservableProperty] private double _wfGain = 50;
    [ObservableProperty] private double _wfContrast = 40;
    [ObservableProperty] private int _wfSpeedIndex;            // 0.2 / 0.5 / 1 s per line
    public double WfLineSeconds => WfSpeedIndex switch { 1 => 0.5, 2 => 1.0, _ => 0.2 };
    public string[] WfSpeeds { get; } = [L.T("1 ligne / 0,2 s"), L.T("1 ligne / 0,5 s"), L.T("1 ligne / 1 s")];
    partial void OnWfSpeedIndexChanged(int value) { OnPropertyChanged(nameof(WfLineSeconds)); OnPropertyChanged(nameof(WfInfo)); Touch(); }
    // the band: around the zones, or a fixed band (WfBandKeys)
    [ObservableProperty] private int _wfBandIndex;
    static readonly string[] WfBandKeys = ["zones", "200-2000", "300-2700"];
    public string[] WfBands { get; } = [L.T("Autour des zones"), "200 – 2000 Hz",
                                        $"{Controls.WaterfallView.FullBandMin:0} – {Controls.WaterfallView.FullBandMax:0} Hz"];
    public double WfFixedLo => WfBandIndex switch { 1 => 200, 2 => Controls.WaterfallView.FullBandMin, _ => 0 };
    public double WfFixedHi => WfBandIndex switch { 1 => 2000, 2 => Controls.WaterfallView.FullBandMax, _ => 0 };
    partial void OnWfBandIndexChanged(int value)
    {
        OnPropertyChanged(nameof(WfFixedLo)); OnPropertyChanged(nameof(WfFixedHi)); OnPropertyChanged(nameof(WfInfo)); Touch();
    }
    // the lines of the waterfall (one per pixel of its height: from the view)
    [ObservableProperty] private int _wfRows = 300;
    partial void OnWfRowsChanged(int value) => OnPropertyChanged(nameof(WfInfo));
    [ObservableProperty] private int _wfMinuteLines = 1;        // Réglages › Interface
    // the text beside the title: the time shown (the band is in its own
    // box; testers: a longer text sent the controls to a second line in
    // the detached window); "réception en cours" only when folded, the
    // waterfall's own label saying it when open
    public string WfInfo =>
        L.F("{0:0} s affichées", WfRows * WfLineSeconds) + (ReceivingNow && !WaterfallShown ? L.T(" · réception en cours") : "");
    // the band shown (the zones, or a fixed band)
    public (double lo, double hi) WfBand =>
        Controls.WaterfallView.Range(WfFixedLo, WfFixedHi, WfCenter, 1000, WfShowZone, WfHalfWidth, WfShowPi4, WfPi4Center, WfPi4HalfWidth);

    // spots
    public ObservableCollection<SpotViewModel> Spots { get; } = new();
    private List<SpotViewModel> _cleared = new();
    [ObservableProperty] private bool _clearedBanner;
    [ObservableProperty] private string _clearedText = "";
    [ObservableProperty] private string _spotsSummary = "";
    [ObservableProperty] private bool _receivingNow;          // "frame in progress" (MainWindowViewModel.Live.cs)
    [ObservableProperty] private string _receivingText = "";
    int _decodedToday;

    // journal
    public ObservableCollection<string> Journal { get; } = new();
    [ObservableProperty] private string _journalSummary = "";
    [ObservableProperty] private string _lastWarning = "";
    int _warnings;

    public string FooterEngine => Engine.StatusText;

    // pass statistics
    double _lastPass, _longestPass;
    int _passes;
    // level watch
    DateTime _lowSince = DateTime.MaxValue;
    bool _lowWarned, _satWarned;
    string? _levelWarning;                          // the line of the level warning, cleared when it is over
    DateTime _lastWarningUtc, _satSince;
    // CPU
    TimeSpan _cpu0;
    DateTime _cpuT0;

    // settings: those of `path` (default: Settings.DefaultPath), or the
    // ones given (then saved to `path` only if one is given)
    public MainWindowViewModel(Settings? settings = null, string? path = null)
    {
        string? problem = null;
        ConfigPath = path ?? Settings.DefaultPath;
        _book = new LocatorBook(Path.Combine(Path.GetDirectoryName(Path.GetFullPath(ConfigPath))!, "locators.json"));
        _book.Message += m => Dispatcher.UIThread.Post(() => Log(m, warning: true));
        _cfg = settings ?? Settings.Load(ConfigPath, out problem);
        _csv = new CsvLog(_cfg.Reports.LogFolder);
        _saveTimer.Tick += (_, _) => SaveNow();
        LoadFields();
        InitTx();
        _loading = false;
        if (Engine.Loaded) OpenLinks();
        if (problem is not null) Log(problem, warning: true);
        if (Callsign == "") Log(L.T("indicatif et locator à régler : Fichier › Réglages › Station"), warning: true);

        UpdateSummary();
        UpdateFooter();
        Utc = DateTime.UtcNow.ToString("HH:mm:ss");
        _cpu0 = Process.GetCurrentProcess().TotalProcessorTime;
        _cpuT0 = DateTime.UtcNow;
        StartUpdateChecks();

        if (!Engine.Loaded)
        {
            Log(L.T("bibliothèques absentes : ") + Engine.Error, warning: true);
            return;
        }
        _rx = new Receiver();
        _rx.Message += m => Dispatcher.UIThread.Post(() => Log(m));
        _rx.SpotDecoded += s => Dispatcher.UIThread.Post(() => AddSpot(s));
        _rx.PassDone += p => Dispatcher.UIThread.Post(() => OnPass(p));
        _rx.KeepPass = KeepWav;
        _rx.Pi4Seen += (c, t) => Dispatcher.UIThread.Post(() => OnPi4Active(c, t));
        _rx.KeyedSeen += (k, t) => Dispatcher.UIThread.Post(() => OnKeyed(k, t));
        ApplySettings();

        var fast = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(100) };
        fast.Tick += (_, _) => { Meter(); TxTick(); ReplayTick(); };
        fast.Start();
        var slow = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        slow.Tick += (_, _) => Second();
        slow.Start();

        // the device list (PortAudio's first use can take a second): off the interface thread
        Task.Run(() =>
        {
            List<(int index, string name, bool isDefault)> devs;
            try { devs = Receiver.InputDevices(); }
            catch (Exception e) { devs = new(); Program.Log(e, "devices"); }
            Dispatcher.UIThread.Post(() => FillSources(devs));
        });
    }

    void FillSources(List<(int index, string name, bool isDefault)> devs)
    {
        foreach (var d in devs) Sources.Add(new SourceItem(d.name, d.index));
        Sources.Add(new SourceItem(L.T("Fichier WAV…"), -1, IsFile: true));
        if (devs.Count == 0)
            Log(LibTwist.AudioAvailable() == 0 ? L.T("audio en direct absent de libtwcore") : L.T("aucune entrée audio trouvée"), warning: true);
        else
            Log(L.F("{0} entrée(s) audio", devs.Count));
        // the saved device, else the default one
        var saved = _cfg.Current.Audio.Input;
        var def = saved == "" ? -1 : devs.FindIndex(d => d.name == saved);
        if (def < 0 && saved != "") Log(L.F("entrée « {0} » absente : entrée par défaut", saved), warning: true);
        if (def < 0) def = devs.FindIndex(d => d.isDefault);
        _suppressSourceChange = true;
        SelectedSource = devs.Count > 0 ? Sources[def >= 0 ? def : 0] : null;
        _suppressSourceChange = false;
        Restart();
    }
    bool _suppressSourceChange;

    partial void OnSelectedSourceChanged(SourceItem? value)
    {
        if (_suppressSourceChange) return;
        if (value is { IsFile: false, Path: null })
        {
            _cfg.Current.Audio.Input = value.Name;    // the device chosen here is remembered
            Touch();
        }
        if (value is { IsFile: true }) _ = OpenWavFile();
        else Restart();
    }

    partial void OnDecodingActiveChanged(bool value)
    {
        if (!_suppressSourceChange) Restart();
    }

    // (re)starts the capture on the selected device, or stops it
    void Restart()
    {
        if (_rx is null) return;
        try
        {
            _rx.Stop();
            ClearLive();
            if (DecodingActive && SelectedSource is { IsFile: false, Path: string path })
            {
                _replaySpots = 0;
                _rx.StartFile(path, ReplayFast);
            }
            else if (DecodingActive && SelectedSource is { IsFile: false } s)
                _rx.StartDevice(s.Device, s.Name);
            else if (!DecodingActive)
                Log(L.T("décodage arrêté"));
        }
        catch (Exception e) { Log(L.T("audio : ") + e.Message, warning: true); }
        UpdateFooter();
    }

    [RelayCommand]
    public async Task OpenWavFile()
    {
        if (_rx is null || PickWavFile is null) return;
        var path = await PickWavFile();
        if (path is null) return;
        SelectFile(path);
    }

    // the file becomes an entry of the Source list, selected: choosing it
    // again replays it
    void SelectFile(string path, bool fast = false)
    {
        var item = Sources.FirstOrDefault(x => x.Path == path)
                   ?? new SourceItem(L.T("Fichier : ") + System.IO.Path.GetFileName(path), -1, Path: path);
        if (!Sources.Contains(item)) Sources.Insert(Math.Max(0, Sources.Count - 1), item);
        _suppressSourceChange = true;
        SelectedSource = item;
        DecodingActive = true;
        _suppressSourceChange = false;
        _replaySpots = 0;
        try { _rx!.StartFile(path, fast || ReplayFast); }
        catch (Exception e) { Log(L.T("fichier : ") + e.Message, warning: true); }
        UpdateFooter();
    }

    // Replays a file directly (tests, screenshots); fast: no real-time pacing.
    public void ReplayFile(string path, bool fast = false)
    {
        if (_rx is null) return;
        SelectFile(path, fast);
    }

    public bool ReceiverRunning => _rx?.Running ?? false;

    // the zones of the waterfall follow the protocols searched
    void Zones(Services.DecodingSettings d)
    {
        WfShowZone = d.Classic || d.TwistS || d.TwistL;
        WfShowPi4 = d.Pi4;
        WfPi4Center = d.Pi4CenterHz;
        WfPi4HalfWidth = d.Pi4WidthHz;
        OnPropertyChanged(nameof(WfInfo));
    }

    // ---- settings from the toolbar ----

    partial void OnSearchClassicChanged(bool value) => ApplySettings();
    partial void OnSearchTwistSChanged(bool value) => ApplySettings();
    partial void OnSearchTwistLChanged(bool value) => ApplySettings();
    partial void OnSearchPi4Changed(bool value) => ApplySettings();
    partial void OnCenterHzChanged(string value) => ApplySettings();
    partial void OnWidthHzChanged(string value) => ApplySettings();
    partial void OnRadioFrequencyChanged(string value)
    {
        Volatile.Write(ref _radioMHz, Num(value) is double m && m > 0 ? m : 0);   // for the decoding thread (WAV names)
        Touch();
    }
    double _radioMHz;
    partial void OnWaterfallOpenChanged(bool value) { OnPropertyChanged(nameof(WfInfo)); Touch(); }
    partial void OnJournalOpenChanged(bool value) => Touch();
    partial void OnWaterfallDetachedChanged(bool value) { OnPropertyChanged(nameof(WfInfo)); Touch(); }
    partial void OnSpotsDetachedChanged(bool value) => Touch();
    partial void OnJournalDetachedChanged(bool value) => Touch();
    partial void OnWfGainChanged(double value) => Touch();
    partial void OnWfContrastChanged(double value) => Touch();

    static double? Num(string s) =>
        double.TryParse(s.Replace(" ", "").Replace(',', '.'), NumberStyles.Float, CultureInfo.InvariantCulture, out var v) ? v : null;

    // the toolbar values (and the decoding settings of the profile) to
    // the receiver, and back to the settings
    void ApplySettings()
    {
        if (_loading) return;
        var d = _cfg.Current.Decoding;
        d.Classic = SearchClassic; d.TwistS = SearchTwistS; d.TwistL = SearchTwistL; d.Pi4 = SearchPi4;
        if (Num(CenterHz) is double c && c > 100 && c < 5000) d.CenterHz = c;
        if (Num(WidthHz) is double w && w >= 10 && w <= 2000) d.WidthHz = w;
        WfCenter = d.CenterHz;
        WfHalfWidth = d.WidthHz;
        Zones(d);
        OnPropertyChanged(nameof(WfInfo));
        Touch();
        _book.SetList(_cfg.Station.KnownCalls);
        _book.AskFbbspots = _cfg.Station.LocatorLookup;
        _book.Api = _cfg.Station.LocatorApi is { Length: > 0 } api ? api : LocatorBook.DefaultApi;
        if (_rx is null) return;
        var s = _rx.Settings;
        s.Protocols = ((d.Classic ? TwlProto.Classic : 0) | (d.TwistS ? TwlProto.TwistS : 0) | (d.TwistL ? TwlProto.TwistL : 0)
                    | (d.Pi4 ? TwlProto.Pi4 : 0)) & Engine.Available;
        s.FCenter = d.CenterHz;
        s.Pi4FCenter = d.Pi4CenterHz;
        s.Pi4Tol = d.Pi4WidthHz;
        s.Tol = d.WidthHz;
        s.SpreadHz = d.SpreadHz;
        _rx.Settings = s;
        _rx.KnownCalls = LocatorBook.CallsOnly(_cfg.Station.KnownCalls);   // (without the locators of the list)
        _rx.PeriodSeconds = d.PassSeconds;
        _rx.ChainDelaySeconds = d.ChainDelaySeconds;
        UpdateFooter();
    }

    // ---- settings ----

    // the fields of the window from the settings
    void LoadFields()
    {
        bool was = _loading;
        _loading = true;
        var p = _cfg.Current;
        SearchClassic = p.Decoding.Classic;
        SearchTwistS = p.Decoding.TwistS;
        SearchTwistL = p.Decoding.TwistL;
        SearchPi4 = p.Decoding.Pi4;
        CenterHz = p.Decoding.CenterHz.ToString("0.#", Inv);
        WidthHz = p.Decoding.WidthHz.ToString("0.#", Inv);
        WfCenter = p.Decoding.CenterHz;
        WfHalfWidth = p.Decoding.WidthHz;
        Zones(p.Decoding);
        RadioFrequency = FormatMHz(p.Radio.FrequencyMHz);
        Volatile.Write(ref _radioMHz, p.Radio.FrequencyMHz);   // (no change event if the text is the same)
        _cfg.Profile = _cfg.Current.Name;           // (an unknown name becomes the first profile)
        // the list rebuilt only if the names changed: emptied, the combo box
        // lost its selection and showed nothing (testers, 0.18)
        var names = _cfg.Profiles.Select(x => x.Name).ToList();
        if (!names.SequenceEqual(ProfileNames))
        {
            ProfileNames.Clear();
            foreach (var n in names) ProfileNames.Add(n);
        }
        CurrentProfile = _cfg.Profile;
        var shown = _cfg.Profile;                   // and shown again once the combo box has settled
        Dispatcher.UIThread.Post(() =>
        {
            if (CurrentProfile == shown) return;
            var was = _loading; _loading = true; CurrentProfile = shown; _loading = was;
        });
        OnPropertyChanged(nameof(SeveralProfiles));
        var ui = _cfg.Interface;
        ApplyTheme(ui.Theme);
        WaterfallOpen = ui.WaterfallOpen;
        JournalOpen = ui.JournalOpen;
        ShowTech = ui.ShowTech;
        WaterfallDetached = ui.WaterfallWindow.Detached;
        SpotsDetached = ui.SpotsWindow.Detached;
        JournalDetached = ui.JournalWindow.Detached;
        WfGain = Math.Clamp(ui.Waterfall.Gain, 0, 100);
        WfContrast = Math.Clamp(ui.Waterfall.Contrast, 0, 100);
        WfSpeedIndex = ui.Waterfall.LineSeconds >= 1 ? 2 : ui.Waterfall.LineSeconds >= 0.5 ? 1 : 0;
        var band = ui.Waterfall.Band != "" ? ui.Waterfall.Band : ui.Waterfall.FullBand ? "300-2700" : "zones";
        WfBandIndex = Math.Max(0, Array.IndexOf(WfBandKeys, band));
        WfMinuteLines = Math.Clamp(ui.Waterfall.MinuteLines, 0, 60);
        _csv.Folder = _cfg.Reports.LogFolder;
        _wav.Folder = Path.Combine(_cfg.Reports.LogFolder, "wav");
        _wav.Days = _cfg.Reports.WavDays;
        _wav.MaxBytes = (long)_cfg.Reports.WavMaxMB << 20;
        OnPropertyChanged(nameof(ProfileText));
        OnPropertyChanged(nameof(WfInfo));
        _loading = was;
    }

    // a value of the window changed: back to the settings, saved soon
    void Touch()
    {
        if (_loading) return;
        var p = _cfg.Current;
        if (Num(RadioFrequency) is double mhz && mhz > 0) p.Radio.FrequencyMHz = mhz;
        var ui = _cfg.Interface;
        ui.WaterfallOpen = WaterfallOpen;
        ui.JournalOpen = JournalOpen;
        ui.ShowTech = ShowTech;
        ui.WaterfallWindow.Detached = WaterfallDetached;
        ui.SpotsWindow.Detached = SpotsDetached;
        ui.JournalWindow.Detached = JournalDetached;
        ui.Waterfall.Gain = Math.Round(WfGain);
        ui.Waterfall.Contrast = Math.Round(WfContrast);
        ui.Waterfall.LineSeconds = WfLineSeconds;
        ui.Waterfall.Band = WfBandKeys[Math.Clamp(WfBandIndex, 0, WfBandKeys.Length - 1)];
        ui.Waterfall.FullBand = WfBandIndex == 2;            // (for 0.23 and before)
        _saveTimer.Stop();
        _saveTimer.Start();
    }

    public void SaveNow()
    {
        _saveTimer.Stop();
        try { _cfg.Save(ConfigPath); }
        catch (Exception e) { Log(L.T("réglages non enregistrés : ") + e.Message, warning: true); }
    }

    // the settings dialog was saved: everything applied, the capture
    // restarted if the input changed
    public void ApplyConfig(Settings c)
    {
        var oldInput = _cfg.Current.Audio.Input;
        if (c.Interface.Language != _cfg.Interface.Language)
            Log(c.Interface.Language == "en" ? "Language: English from the next start of the program"
                                             : "Langue : français au prochain démarrage du programme");   // no-tr
        _cfg = c;
        LoadFields();
        ApplySettings();
        TxSettingsChanged();
        OpenLinks();
        SaveNow();
        Log(L.T("réglages enregistrés"));
        if (Callsign == "") Log(L.T("indicatif et locator à régler : Fichier › Réglages › Station"), warning: true);
        var input = _cfg.Current.Audio.Input;
        if (input != oldInput && SelectedSource is { IsFile: false, Path: null })
        {
            int i = input == "" ? -1 : Sources.ToList().FindIndex(x => x.Name == input && !x.IsFile);
            var item = i >= 0 ? Sources[i] : Sources.FirstOrDefault(x => !x.IsFile && x.Path is null);
            if (item is not null && item != SelectedSource)
            {
                _suppressSourceChange = true;
                SelectedSource = item;
                _suppressSourceChange = false;
                Restart();
            }
        }
        UpdateFooter();
        ConfigApplied?.Invoke();
    }

    // new settings, or another profile: the Balise window takes them again
    public event Action? ConfigApplied;

    // dark, light, or as the system (Windows' light / dark mode)
    public static void ApplyTheme(string theme)
    {
        if (Avalonia.Application.Current is not { } app) return;
        app.RequestedThemeVariant = theme switch
        {
            "light" => Avalonia.Styling.ThemeVariant.Light,
            "system" => Avalonia.Styling.ThemeVariant.Default,
            _ => Avalonia.Styling.ThemeVariant.Dark,
        };
    }

    // ---- profiles ----

    public System.Collections.ObjectModel.ObservableCollection<string> ProfileNames { get; } = new();
    [ObservableProperty] private string? _currentProfile;
    public bool SeveralProfiles => ProfileNames.Count > 1;

    // another profile chosen in the title bar: everything that depends on
    // it is applied (sound card, radio, decoding, beacon)
    partial void OnCurrentProfileChanged(string? value)
    {
        if (_loading || value is null || value == _cfg.Profile) return;
        if (_beacon?.Active == true || CycleRunning)
        {
            Log(L.T("profil non changé : émission ou cycle en cours"), warning: true);
            Dispatcher.UIThread.Post(() => { _loading = true; CurrentProfile = _cfg.Profile; _loading = false; });
            return;
        }
        var c = _cfg.Clone();
        c.Profile = value;
        ApplyConfig(c);
        Log(L.F("profil « {0} »", value));
    }

    // device names for the settings dialog
    public IReadOnlyList<string> InputNames => Sources.Where(x => !x.IsFile && x.Path is null).Select(x => x.Name).ToList();

    // 144.489 -> "144,489 000"
    public static string FormatMHz(double mhz)
    {
        var t = mhz.ToString("0.000000", Fr);
        return t[..^3] + " " + t[^3..];
    }

    // ---- CSV journal ----

    [RelayCommand]
    void OpenCsv()
    {
        var file = _csv.PathFor(DateTime.UtcNow);
        var target = File.Exists(file) ? file : _csv.Folder;
        try
        {
            Directory.CreateDirectory(_csv.Folder);
            // Windows, macOS: the shell opens the file with the spreadsheet
            // (or the folder in the explorer); Linux: xdg-open
            var psi = OperatingSystem.IsLinux() ? new ProcessStartInfo("xdg-open") : new ProcessStartInfo(target) { UseShellExecute = true };
            if (OperatingSystem.IsLinux()) psi.ArgumentList.Add(target);
            Process.Start(psi);
        }
        catch (Exception e) { Log(L.T("ouverture du journal : ") + e.Message, warning: true); }
    }

    void UpdateFooter()
    {
        if (_rx is null) { FooterZone = ""; FooterSource = L.T("pas de décodage"); FooterCsv = ""; return; }
        var s = _rx.Settings;
        FooterZone = $"Zone {s.FCenter - s.Tol:0} – {s.FCenter + s.Tol:0} Hz"
                     + (s.Protocols.HasFlag(TwlProto.Pi4) ? $" · PI4 {s.Pi4FCenter - s.Pi4Tol:0} – {s.Pi4FCenter + s.Pi4Tol:0} Hz" : "");
        FooterSource = _rx.Running ? L.F("Réception : {0} · passe toutes les {1:0.#} s", _rx.SourceName, _rx.PeriodSeconds) : L.T("Réception arrêtée");
        FooterCsv = _cfg.Reports.CsvLog ? L.F("Journal CSV : {0}", _csv.PathFor(DateTime.UtcNow)) : L.T("Journal CSV : arrêté (Réglages › Rapports)");
    }

    // ---- events from the receiver ----

    // PI4 beacons alternate their callsign and "/locator" from one minute
    // to the next, on the same frequency: a locator message is given the
    // callsign of the PI4 frame of that beacon received before it, or after
    // it (the line then waits, Pi4Waiting). Same beacon: Pi4Pair.
    readonly List<(string call, double freq, DateTime start)> _pi4Calls = new();
    readonly List<(SpotViewModel vm, Spot s)> _pi4Waiting = new();
    const double Pi4WaitMinutes = 3.5;

    // two PI4 frames of the same beacon? Both start on the minute (PI4
    // frames are sent at second 1 of the minute), so 1 to 3 whole minutes
    // apart (2.5 s at most beside), within 6 Hz. Null if not, else a cost
    // (the lower the better): the frequency gap, a minute more counted 2 Hz.
    public static double? Pi4Pair(double dtSeconds, double dfHz)
    {
        double a = Math.Abs(dtSeconds);
        int k = (int)Math.Round(a / 60);
        if (k < 1 || k > 3 || Math.Abs(a - 60 * k) > 2.5 || Math.Abs(dfHz) > 6) return null;
        return Math.Abs(dfHz) + 2 * (k - 1);
    }

    string? Pi4CallFor(Spot s)
    {
        string? best = null;
        double bestCost = double.MaxValue;
        foreach (var (call, freq, start) in _pi4Calls)
            if (start < s.StartUtc && Pi4Pair((s.StartUtc - start).TotalSeconds, s.FreqHz - freq) is double c && c < bestCost)
                (best, bestCost) = (call, c);
        return best;
    }

    static string Pi4Tip(string loc, string? call, bool after) => call is null
        ? L.F("Message PI4 « /{0} » : locator seul ; l'indicatif sera celui de la trame PI4 de cette balise (même fréquence, minute ronde) reçue dans les 3 minutes", loc)
        : after ? L.F("Message PI4 « /{0} » : locator reçu ; indicatif {1} de la trame PI4 de cette balise reçue après (même fréquence, minute ronde)", loc, call)
                : L.F("Message PI4 « /{0} » : locator reçu ; indicatif {1} de la trame PI4 de cette balise reçue avant (même fréquence, minute ronde)", loc, call);

    // a PI4 callsign frame: the locator lines waiting for it get it
    void Pi4Complete(Spot c)
    {
        foreach (var w in _pi4Waiting.ToList())
        {
            if (w.s.StartUtc >= c.StartUtc || Pi4Pair((c.StartUtc - w.s.StartUtc).TotalSeconds, c.FreqHz - w.s.FreqHz) is null) continue;
            _pi4Waiting.Remove(w);
            var loc = w.s.Loc.ToUpperInvariant();
            w.vm.Call = c.Call.ToLowerInvariant();
            w.vm.CallGuessed = true;
            w.vm.LocTip = Pi4Tip(loc, c.Call, after: true);
            var done = w.s with { Call = c.Call.ToLowerInvariant() };
            _book.Heard(c.Call, w.s.Loc);
            PublishSpot(done, Num(RadioFrequency) is double fmhz && fmhz > 0 ? fmhz + done.FreqHz / 1e6 : null);
            WriteCsv(done);
            Log(L.F("PI4 /{0} : indicatif {1} (trame suivante)", loc, c.Call));
        }
    }

    // locator lines without a callsign for too long: written to the CSV
    // journal as they are (now: the time of the newest audio)
    void Pi4Expire(DateTime now, bool all = false)
    {
        foreach (var w in _pi4Waiting.Where(w => all || (now - w.s.StartUtc).TotalMinutes > Pi4WaitMinutes).ToList())
        {
            _pi4Waiting.Remove(w);
            WriteCsv(w.s);
        }
    }

    void AddSpot(Spot s)
    {
        if (SelectedSource is { Path: not null }) _replaySpots++;
        bool pi4Loc = false;
        if (s.Proto == TwlProto.Pi4)
        {
            Pi4Heard(s.FreqHz, s.StartUtc.AddSeconds(FrameSeconds(s.Proto)));
            if (s.Call == "" && s.Loc != "")
            {
                pi4Loc = true;
                s = s with { Call = Pi4CallFor(s) ?? "" };
            }
        }
        bool guessed = pi4Loc && s.Call != "";
        // what is shown, journaled and sent: a guessed callsign in small
        // letters (like a guessed locator); s keeps it in capitals
        var shown = guessed ? s with { Call = s.Call.ToLowerInvariant() } : s;
        string? dist = null, az = null;
        var da = s.Loc != "" && Locator != "" ? Maidenhead.DistanceAzimuth(Locator, s.Loc) : null;
        if (da is { } v)
        {
            dist = $"{v.km:0} km";
            az = $"{v.az:0}°";
        }
        // a ghost? never for a callsign of the list (Réglages › Station) or
        // found by the deep search, which looks only for those
        // (nor for a callsign given to a PI4 locator: its own frame was checked)
        var doubt = s.Deep || guessed || IsKnown(s.Call) || s.Call == "" ? null : Services.CallCheck.Doubt(s.Call, s.Loc == "" ? null : s.Loc);
        var spot = new SpotViewModel
        {
            Doubt = doubt,
            Start = NearestSecond(s.StartUtc).ToString("HH:mm:ss"),
            Proto = s.Proto,
            Deep = s.Deep,
            Call = shown.Call == "" ? "—" : shown.Call,              // (a PI4 locator alone)
            CallGuessed = guessed,
            Loc = s.Loc == "" ? "—" : s.Loc.ToUpperInvariant(),      // received: capitals
            LocTip = !pi4Loc ? null : Pi4Tip(s.Loc.ToUpperInvariant(), s.Call == "" ? null : s.Call, after: false),
            Snr = (s.Snr < 0 ? "−" : "") + Math.Abs(Math.Round(s.Snr)).ToString("0"),
            Audio = s.FreqHz.ToString("0"),
            Rf = Rf(s.FreqHz),
            Distance = dist ?? "—",
            Azimuth = az ?? "—",
        };
        Spots.Insert(0, spot);
        if (doubt is not null) { }                   // nothing remembered or asked for a doubtful callsign
        else if (s.Call == "") { }                   // a PI4 locator without its callsign
        else if (s.Loc != "") _book.Heard(s.Call, s.Loc);
        else FindLocator(spot, s.FreqHz);
        _decodedToday++;
        // label on the waterfall, at the moment the frame ended
        // (plus the delay it was dated back by: the waterfall shows what arrives)
        var end = s.StartUtc.AddSeconds(FrameSeconds(s.Proto) + s.DelayS);
        // (a PI4 locator message: what it holds, "/JN12AX")
        Marks.Add(new Controls.WaterfallMark(s.FreqHz, end, (pi4Loc ? "/" + s.Loc.ToUpperInvariant() : spot.CallText) + " · " + Engine.Label(s.Proto),
                                             s.Proto == TwlProto.Classic));
        LiveDecoded(s.FreqHz, s.StartUtc.AddSeconds(FrameSeconds(s.Proto)));
        while (Marks.Count > 30) { var old = Marks.FirstOrDefault(x => !x.InProgress); if (old is null) break; Marks.Remove(old); }
        ClearedBanner = false;
        UpdateSummary();
        Log($"{Engine.Label(s.Proto)} {shown.Call} {s.Loc} {Math.Round(s.Snr):0} dB {s.FreqHz:0} Hz".Replace("  ", " ")
            + (doubt is null ? "" : L.F(" — douteux : {0}", doubt)));

        if (doubt is null && s.Call != "")
        {
            PublishSpot(shown, Num(RadioFrequency) is double fmhz && fmhz > 0 ? fmhz + s.FreqHz / 1e6 : null);
            // the chime, for spots received live only (not a file replayed)
            if (_cfg.Interface.SpotSound && SelectedSource is { IsFile: false, Path: null }) Services.Chime.Play();
        }
        // PI4: a callsign frame completes the locator lines waiting for it;
        // a locator line without callsign waits for it (journaled then)
        if (s.Proto == TwlProto.Pi4 && !pi4Loc && doubt is null)
        {
            _pi4Calls.Add((s.Call, s.FreqHz, s.StartUtc));
            if (_pi4Calls.Count > 50) _pi4Calls.RemoveAt(0);
            Pi4Complete(s);
        }
        if (pi4Loc && s.Call == "") _pi4Waiting.Add((spot, s));
        else WriteCsv(shown, doubt);
    }

    void WriteCsv(Spot s, string? doubt = null)
    {
        if (!_cfg.Reports.CsvLog) return;
        var da = s.Loc != "" && Locator != "" ? Maidenhead.DistanceAzimuth(Locator, s.Loc) : null;
        try
        {
            double? rf = Num(RadioFrequency) is double mhz && mhz > 0 ? mhz + s.FreqHz / 1e6 : null;
            _csv.Append(s with { StartUtc = NearestSecond(s.StartUtc) }, Engine.Label(s.Proto), rf, da?.km, da?.az, _rx?.SourceName ?? "", doubt);
            _csvFailed = false;
        }
        catch (Exception e)
        {
            if (!_csvFailed) Log(L.T("journal CSV : ") + e.Message, warning: true);
            _csvFailed = true;
        }
    }
    bool _csvFailed;

    // duration of a frame (s): chips for the OOK frames, 146 symbols of 1/6 s for PI4
    public static double FrameSeconds(TwlProto p) =>
        p == TwlProto.Pi4 ? 146.0 / 6.0 : LibTwist.FrameChips(p) * LibTwist.ChipSeconds();

    // start times are shown to the nearest second: a frame sent on the
    // second and decoded 30 ms early reads :00, not :59 of the minute before
    public static DateTime NearestSecond(DateTime t) =>
        new DateTime((t.Ticks + TimeSpan.TicksPerSecond / 2) / TimeSpan.TicksPerSecond * TimeSpan.TicksPerSecond, t.Kind);

    // RF frequency = radio frequency (entered for now; CAT at step 6) + audio
    string Rf(double audioHz) =>
        Num(RadioFrequency) is double mhz && mhz > 0 ? FormatMHz(mhz + audioHz / 1e6) : "—";   // 144,490 520

    void OnPass(PassInfo p)
    {
        Pi4Expire(p.EndUtc);
        _lastPass = p.Seconds;
        _longestPass = Math.Max(_longestPass, p.Seconds);
        if (++_passes % 20 == 0)
        {
            Log(L.F("{0} passes, la plus longue {1} s", _passes, _longestPass.ToString("0.00", Fr)));
            _longestPass = 0;
        }
    }

    void Meter()
    {
        if (_rx is null || !_rx.Running) { LevelDbfs = -120; PeakDbfs = -120; return; }
        var st = _rx.Stats;
        LevelDbfs = st.LevelDbfs;
        PeakDbfs = Math.Max(st.LevelDbfs, PeakDbfs - 0.5);     // peak hold, falls 5 dB/s
    }

    void Second()
    {
        RecordTick();
        var now = DateTime.UtcNow;
        Utc = now.ToString("HH:mm:ss");

        var cpu = Process.GetCurrentProcess().TotalProcessorTime;
        double used = (cpu - _cpu0).TotalSeconds / Math.Max(0.001, (now - _cpuT0).TotalSeconds) / Environment.ProcessorCount;
        _cpu0 = cpu; _cpuT0 = now;
        CpuPercent = Math.Clamp(used * 100, 0, 100);
        CpuText = $"{used * 100:0} %";
        UpdateCat();

        if (_rx is null || !_rx.Running) { PassText = "—"; AudioText = "—"; if (_live.Count > 0) ClearLive(); UpdateFooter(); return; }
        var st = _rx.Stats;
        int next = Math.Max(0, (int)Math.Ceiling((_rx.NextPassUtc - now).TotalSeconds));
        PassText = _passes == 0
            ? (st.Seconds < 31 ? L.F("début dans {0:0} s", Math.Ceiling(31 - st.Seconds)) : L.F("prochaine dans {0} s", next))
            : L.F("{0} s · prochaine dans {1} s", _lastPass.ToString("0.00", Fr), next);
        AudioText = L.F("{0} kHz · pertes : {1}", (st.Rate / 1000.0).ToString("0.###", Fr), st.Overruns);
        UpdateFooter();

        // level watch: too low for 10 s, saturation
        if (st.LevelDbfs < Controls.LevelMeter.Low)
        {
            if (_lowSince == DateTime.MaxValue) _lowSince = now;
            if (!_lowWarned && (now - _lowSince).TotalSeconds > 10)
            {
                Log(L.F("niveau d'entrée bas ({0:0} dBFS)", st.LevelDbfs).Replace("-", "−"), warning: true);
                _levelWarning = LastWarning;
                _lowWarned = true;
            }
        }
        else
        {
            _lowSince = DateTime.MaxValue;
            if (_lowWarned) LevelOk();
            _lowWarned = false;
        }
        if (st.LevelDbfs > -1.0)                       // clipping, not just the red zone
        {
            if (!_satWarned) { Log(L.T("saturation de l'entrée audio"), warning: true); _levelWarning = LastWarning; }
            _satWarned = true;
            _satSince = now;
        }
        else if (_satWarned && (now - _satSince).TotalSeconds > 10) { LevelOk(); _satWarned = false; }

        // a warning shown in the spots header goes away after 15 minutes
        if (LastWarning != "" && (DateTime.UtcNow - _lastWarningUtc).TotalMinutes > 15) LastWarning = "";
    }

    // ---- spots list and journal ----

    [RelayCommand]
    private void ClearSpots()
    {
        _cleared = Spots.ToList();
        Spots.Clear();
        ClearedText = L.F("Liste vidée à {0:HH:mm:ss} — les spots déjà affichés restent dans le journal.", DateTime.UtcNow);
        ClearedBanner = true;
        UpdateSummary();
    }

    [RelayCommand]
    private void UndoClear()
    {
        foreach (var s in _cleared) Spots.Add(s);
        _cleared.Clear();
        ClearedBanner = false;
        UpdateSummary();
    }

    // a callsign of the list of Réglages › Station
    bool IsKnown(string call)
    {
        var c = call.Trim().ToUpperInvariant();
        return LocatorBook.ParseList(_cfg.Station.KnownCalls).Any(x => x.call == c);
    }

    // clic droit › Supprimer la ligne (or the Delete key): the line leaves
    // the list, the spot stays in the CSV journal
    [ObservableProperty] private SpotViewModel? _selectedSpot;
    [RelayCommand]
    private void DeleteSpot(SpotViewModel? spot)
    {
        spot ??= SelectedSpot;
        if (spot is null || !Spots.Remove(spot)) return;
        // (a PI4 locator line waiting for its callsign: journaled as it is)
        foreach (var w in _pi4Waiting.Where(w => w.vm == spot).ToList()) { _pi4Waiting.Remove(w); WriteCsv(w.s); }
        Log(L.F("ligne {0} {1} supprimée de la liste (reste dans le journal CSV)", spot.Start, spot.Call));
        UpdateSummary();
    }

    private void UpdateSummary() =>
        SpotsSummary = (_decodedToday <= 1 ? L.F("{0} décodage depuis le lancement", _decodedToday) : L.F("{0} décodages depuis le lancement", _decodedToday))
                       + " · " + (Spots.Count <= 1 ? L.F("{0} affiché", Spots.Count) : L.F("{0} affichés", Spots.Count));

    // the input level is right again: said in the journal, and the level
    // warning no longer shown as the last anomaly (testers, 0.17)
    void LevelOk()
    {
        bool shown = _levelWarning is not null && LastWarning == _levelWarning;
        Log(L.T("niveau d'entrée correct"));
        if (shown) LastWarning = "";
        _levelWarning = null;
    }

    public void Log(string m, bool warning = false)
    {
        var line = $"{DateTime.UtcNow:HH:mm:ss} {m}";
        _session.Add($"{DateTime.UtcNow:yyyy-MM-dd} {line}" + (warning ? "  [!]" : ""));
        if (_session.Count > 5000) _session.RemoveAt(0);
        Journal.Add(line);
        if (Journal.Count > 500) Journal.RemoveAt(0);
        if (warning) { _warnings++; LastWarning = line; _lastWarningUtc = DateTime.UtcNow; }
        JournalSummary = (Journal.Count == 1 ? L.T("1 message") : L.F("{0} messages", Journal.Count))
                       + (_warnings == 0 ? "" : _warnings == 1 ? L.T(" · 1 avertissement") : L.F(" · {0} avertissements", _warnings));
    }

    // ---- kept WAV and testers' report ----

    readonly List<string> _session = new();                // the whole session, for the report
    readonly WavKeeper _wav = new(Path.GetTempPath());

    // on the decoding thread: a live pass that decoded something
    void KeepWav(ReadOnlyMemory<float> x, int fs, DateTime t0, List<Spot> spots)
    {
        if (!_cfg.Reports.KeepWav) return;
        double mhz = Volatile.Read(ref _radioMHz);
        var path = _wav.Save(x.Span, fs, t0, mhz > 0 ? mhz : null, spots.Select(s => (s, Engine.Label(s.Proto))), Report.AppVersion);
        Dispatcher.UIThread.Post(() => Log("WAV : " + Path.GetFileName(path)));
    }

    [RelayCommand]
    void OpenWavFolder() => OpenInShell(_wav.Folder, folder: true);

    // Aide › Créer un rapport: the zip, shown in its folder
    [RelayCommand]
    void CreateReport()
    {
        try
        {
            SaveNow();
            var st = new System.Text.StringBuilder();
            st.AppendLine(L.T("État au moment du rapport :"));
            st.AppendLine(L.F("  Fichier de réglages utilisé : {0}", ConfigPath));
            st.AppendLine($"  Station : {ProfileText}");
            st.AppendLine($"  Source : {SelectedSource?.Name ?? "—"} · {FooterSource}");
            st.AppendLine(L.F("  Audio : {0} · entrée {1}", AudioText, LevelText));
            st.AppendLine($"  Passes : {PassText} · {SpotsSummary}");
            st.AppendLine($"  {FooterZone} · radio {RadioFrequency} MHz");
            st.AppendLine($"  {FooterCat}");
            st.AppendLine(L.F("  Balise : {0} · prochaine : {1}", BadgeText, NextTx));
            st.AppendLine($"  {FooterCsv}");
            var json = File.Exists(ConfigPath) ? File.ReadAllText(ConfigPath) : "{}";
            var csv = _csv.PathFor(DateTime.UtcNow);
            var path = Report.Create(_cfg.Reports.LogFolder, json, _session, st.ToString(), csv, _wav.Newest());
            Log(L.T("rapport créé : ") + path);
            OpenInShell(path, folder: false);
        }
        catch (Exception e) { Log(L.T("rapport impossible : ") + e.Message, warning: true); }
    }

    // a folder in the explorer, or a file selected in its folder
    void OpenInShell(string path, bool folder)
    {
        try
        {
            if (folder) Directory.CreateDirectory(path);
            ProcessStartInfo psi;
            if (OperatingSystem.IsWindows())
                psi = folder ? new ProcessStartInfo("explorer.exe", $"\"{path}\"")
                             : new ProcessStartInfo("explorer.exe", $"/select,\"{path}\"");
            else
            {
                psi = new ProcessStartInfo(OperatingSystem.IsMacOS() ? "open" : "xdg-open");
                psi.ArgumentList.Add(folder ? path : Path.GetDirectoryName(path)!);
            }
            Process.Start(psi);
        }
        catch (Exception e) { Log(L.T("ouverture impossible : ") + e.Message, warning: true); }
    }

    public void Dispose()
    {
        _beacon?.Stop();
        CloseLinks();                        // PTT released, rigctld stopped
        _beacon?.Dispose();                 // the transmission stops with the program
        if (_saveTimer.IsEnabled) SaveNow();
        StopRecording(L.T("arrêté (fin du programme)"));   // (its file closed, readable)
        Pi4Expire(DateTime.UtcNow, all: true);
        _rx?.Dispose();
    }
}
