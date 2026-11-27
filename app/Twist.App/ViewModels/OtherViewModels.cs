using System.Collections.ObjectModel;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using System.Globalization;
using System.Text.RegularExpressions;
using Twist.App.Services;
using Twist.Native;
using Twist.App.Lang;

namespace Twist.App.ViewModels;

// One step of the beacon sequence (docs/SPECIFICATION.md, section 3),
// edited in the Balise window: a frame (protocol), a carrier or a pause
// (duration, PTT held or released).
public sealed partial class StepViewModel : ObservableObject
{
    readonly BaliseViewModel _owner;
    public StepViewModel(BaliseViewModel owner, StepSettings s)
    {
        _owner = owner;
        Settings = s;
        _protoIndex = Math.Max(0, Array.IndexOf(ProtoKeys, s.Proto));
        _seconds = s.Seconds;
        _pttHold = s.PttHold;
        _pi4MessageIndex = Math.Max(0, Array.IndexOf(Pi4MessageKeys, s.Pi4Message));
        _atPi4 = s.AtPi4;
    }

    public StepSettings Settings { get; }
    [ObservableProperty] private int _index;
    [ObservableProperty] private int _protoIndex;
    [ObservableProperty] private double _seconds;
    [ObservableProperty] private bool _pttHold;
    [ObservableProperty] private int _pi4MessageIndex;
    [ObservableProperty] private bool _atPi4;

    static readonly string[] ProtoKeys = ["twistl", "twists", "classic", "pi4"];
    public string[] ProtoNames { get; } = ["TWIST L", "TWIST S", Engine.Choice(TwlProto.Classic), Engine.Choice(TwlProto.Pi4)];
    static readonly string[] Pi4MessageKeys = ["alt", "call", "loc"];
    public string[] Pi4MessageNames { get; } = [L.T("indicatif et /locator alternés"), L.T("indicatif"), "/locator"];

    public bool IsFrame => Settings.Kind == "frame";
    public bool IsTimed => Settings.Kind is "carrier" or "pause";
    public bool IsPause => Settings.Kind == "pause";
    public bool IsCw => Settings.Kind == "cw";
    public bool IsPi4Frame => IsFrame && Settings.Proto == "pi4";
    // carrier and CW: at the transmission tone or at the PI4 frequency
    public bool HasFrequencyChoice => Settings.Kind is "carrier" or "cw";
    public string KindText => Settings.Kind switch { "frame" => L.T("Trame"), "carrier" => L.T("Porteuse"), "cw" => "CW", _ => L.T("Pause") };
    // colour: TextBlock.proto (.classic, .pi4, .carrier, .pause) in App.axaml
    public bool IsClassicFrame => IsFrame && Settings.Proto == "classic";
    public bool IsCarrier => Settings.Kind is "carrier" or "cw";
    public TwlProto Proto => MainWindowViewModel.ProtoOf(Settings.Proto);
    public double Duration => _owner.StepSeconds(Settings);
    public string DurationText => Duration.ToString("0.0", Fr) + " s";
    public string PttText => Settings.Kind == "pause" ? (Settings.PttHold ? L.T("PTT maintenu") : L.T("PTT relâché")) : "PTT";
    public string Detail => _owner.StepDetail(Settings);
    static CultureInfo Fr => L.Culture;

    partial void OnProtoIndexChanged(int value)
    {
        Settings.Proto = ProtoKeys[Math.Clamp(value, 0, ProtoKeys.Length - 1)];
        OnPropertyChanged(nameof(IsPi4Frame));
        Changed();
    }
    partial void OnPi4MessageIndexChanged(int value)
    {
        Settings.Pi4Message = Pi4MessageKeys[Math.Clamp(value, 0, Pi4MessageKeys.Length - 1)];
        Changed();
    }
    partial void OnAtPi4Changed(bool value) { Settings.AtPi4 = value; Changed(); }
    partial void OnSecondsChanged(double value)
    {
        Settings.Seconds = Math.Clamp(Math.Round(value, 1), 0.1, Settings.Kind == "carrier" ? 600 : 3600);
        Changed();
    }
    partial void OnPttHoldChanged(bool value) { Settings.PttHold = value; Changed(); }

    void Changed()
    {
        OnPropertyChanged(nameof(IsClassicFrame));
        OnPropertyChanged(nameof(Duration));
        OnPropertyChanged(nameof(DurationText));
        OnPropertyChanged(nameof(PttText));
        OnPropertyChanged(nameof(Detail));
        _owner.StepsChanged();
    }
    public void Refresh()
    {
        OnPropertyChanged(nameof(Detail));
        OnPropertyChanged(nameof(Duration));
        OnPropertyChanged(nameof(DurationText));
    }
}

// The beacon window: transmission in progress (from the main window's
// transmitter), Tune, the sequence and the cycle. Every change goes to
// the settings of the current profile.
public sealed partial class BaliseViewModel : ObservableObject
{
    public MainWindowViewModel Main { get; }
    BeaconSettings B => Main.Config.Current.Beacon;
    public ObservableCollection<StepViewModel> Steps { get; } = new();

    public BaliseViewModel(MainWindowViewModel main)
    {
        Main = main;
        Reload();
        Main.ConfigApplied += Reload;
    }

    // the beacon of the current profile (again after a change of profile
    // or of the settings: B is then the beacon of the new one)
    bool _reloading;
    void Reload()
    {
        _reloading = true;
        PeriodMin = B.PeriodMinutes;
        StartSecond = B.StartSecond;
        MaxSeconds = B.MaxTxSeconds;
        TuneSeconds = B.TuneSeconds;
        ReceiveBetween = B.ReceiveBetween;
        Pi4ToneHz = B.Pi4ToneHz;
        CwWpm = B.CwWpm;
        Steps.Clear();
        foreach (var s in B.Steps) Steps.Add(new StepViewModel(this, s));
        Renumber();
        _reloading = false;
        OnPropertyChanged(nameof(Station));
        OnPropertyChanged(nameof(TotalText));
        OnPropertyChanged(nameof(Warning));
        OnPropertyChanged(nameof(HasWarning));
    }

    // the window closed
    public void Detach() => Main.ConfigApplied -= Reload;

    public string Station =>
        (Main.Callsign == "" ? L.T("indicatif à régler") : Main.Locator == "" ? Main.Callsign : $"{Main.Callsign} · {Main.Locator}")
        + L.F(" · émission audio (USB) · PTT {0}", Main.Config.Current.Radio.Ptt);

    [ObservableProperty] private int _periodMin;
    [ObservableProperty] private int _startSecond;
    [ObservableProperty] private int _maxSeconds;
    [ObservableProperty] private int _tuneSeconds;
    [ObservableProperty] private bool _receiveBetween;
    [ObservableProperty] private double _pi4ToneHz;
    [ObservableProperty] private double _cwWpm;

    partial void OnPeriodMinChanged(int value) { B.PeriodMinutes = Math.Clamp(value, 1, 1440); Saved(); }
    partial void OnStartSecondChanged(int value) { B.StartSecond = Math.Clamp(value, 0, 59); Saved(); }
    partial void OnMaxSecondsChanged(int value) { B.MaxTxSeconds = Math.Clamp(value, 10, 3600); Saved(); }
    partial void OnTuneSecondsChanged(int value) { B.TuneSeconds = Math.Clamp(value, 1, 600); Saved(); }
    partial void OnReceiveBetweenChanged(bool value) { B.ReceiveBetween = value; Saved(); }
    partial void OnPi4ToneHzChanged(double value) { B.Pi4ToneHz = Math.Clamp(Math.Round(value), 200, 2500); Saved(); RefreshSteps(); }
    partial void OnCwWpmChanged(double value) { B.CwWpm = Math.Clamp(Math.Round(value), 5, 40); Saved(); RefreshSteps(); }

    void RefreshSteps()
    {
        if (_reloading) return;
        foreach (var s in Steps) s.Refresh();
        OnPropertyChanged(nameof(TotalText));
    }

    void Saved()
    {
        if (_reloading) return;
        Main.SaveSoon();
        OnPropertyChanged(nameof(TotalText));
        OnPropertyChanged(nameof(Warning));
        OnPropertyChanged(nameof(HasWarning));
    }

    public string FrameDetail(TwlProto p) =>
        Engine.Label(p) + " · " + (Main.Callsign == "" ? L.T("(indicatif)") : Main.Callsign)
        + (p == TwlProto.TwistL ? " " + (Main.Locator == "" ? L.T("(locator)") : Main.Locator) : "");

    string CallText => Main.Callsign == "" ? L.T("(indicatif)") : Main.Callsign;
    string LocText => Main.Locator == "" ? L.T("(locator)") : Main.Locator;

    // what a step sends, shown beside it
    public string StepDetail(StepSettings s)
    {
        double f = B.Pi4ToneHz;
        string at = s.AtPi4 ? L.F("à la fréquence PI4 ({0:0} Hz)", f) : L.T("au ton d'émission");
        return s.Kind switch
        {
            "carrier" => L.T("continue, ") + at,
            "pause" => "silence",
            "cw" => L.F("{0} {1} · {2:0} mots/min, ", CallText, LocText, B.CwWpm) + at,
            _ when s.Proto == "pi4" => s.Pi4Message switch
            {
                "call" => CallText,
                "loc" => "/" + LocText,
                _ => L.F("{0} puis /{1}, un cycle sur deux", CallText, LocText),
            } + L.F(" · {0:0} Hz", f),
            _ => FrameDetail(MainWindowViewModel.ProtoOf(s.Proto)),
        };
    }

    // its duration (a PI4 frame, CW: from libtwist; 0 if it cannot be sent)
    public double StepSeconds(StepSettings s)
    {
        if (s.Kind == "cw" && Main.Callsign == "") return 0;
        var st = Main.StepOf(s, DateTime.UtcNow, out _);
        return Math.Max(0, Services.Beacon.StepSeconds(st));
    }

    double Total => Steps.Sum(s => s.Duration);
    public string TotalText => L.Dec($"{Total:0.0} s");

    // what would stop the cycle from starting, shown under the sequence
    public string Warning
    {
        get
        {
            if (Steps.Count == 0) return L.T("Séquence vide");
            if (Total >= PeriodMin * 60 - 2) return L.F("Séquence plus longue que la période du cycle ({0} min)", PeriodMin);
            var steps = Main.SequenceSteps(out var problem);
            if (problem is not null) return problem;
            double ptt = Services.Beacon.LongestPtt(steps);
            if (ptt > MaxSeconds) return L.F("PTT maintenu {0:0} s sans interruption : plus que le chien de garde ({1} s)", ptt, MaxSeconds);
            return "";
        }
    }
    public bool HasWarning => Warning != "";

    public void StepsChanged()
    {
        B.Steps = Steps.Select(s => s.Settings).ToList();
        Renumber();
        Saved();
    }

    void Renumber()
    {
        for (int i = 0; i < Steps.Count; i++) Steps[i].Index = i + 1;
    }

    void Add(StepSettings s)
    {
        if (Steps.Count >= 32) return;
        Steps.Add(new StepViewModel(this, s));
        StepsChanged();
    }

    [RelayCommand] void AddFrame() => Add(new StepSettings { Kind = "frame", Proto = "twistl" });
    [RelayCommand] void AddCarrier() => Add(new StepSettings { Kind = "carrier", Seconds = 10 });
    [RelayCommand] void AddPause() => Add(new StepSettings { Kind = "pause", Seconds = 5, PttHold = true });
    [RelayCommand] void AddCw() => Add(new StepSettings { Kind = "cw", AtPi4 = Steps.Any(x => x.IsPi4Frame) });

    // the minute of a PI4 beacon (as FbBalise sends it): the PI4 frame
    // at second 0 (callsign and /locator one minute out of two), the CW
    // identification and a carrier at the PI4 frequency, until 57.5 s
    // (the next minute is prepared 1 s ahead) - every minute
    [RelayCommand]
    void Pi4Sequence()
    {
        var pi4 = new StepSettings { Kind = "frame", Proto = "pi4", Pi4Message = "alt" };
        var cw = new StepSettings { Kind = "cw", AtPi4 = true };
        double used = StepSeconds(pi4) + StepSeconds(cw);
        var carrier = new StepSettings { Kind = "carrier", AtPi4 = true, Seconds = Math.Max(1, Math.Floor((Pi4MinuteSeconds - used) * 10) / 10) };
        Steps.Clear();
        foreach (var s in new[] { pi4, cw, carrier }) Steps.Add(new StepViewModel(this, s));
        _reloading = true;
        PeriodMin = 1;
        StartSecond = 0;
        _reloading = false;
        B.PeriodMinutes = 1;
        B.StartSecond = 0;
        if (B.MaxTxSeconds < 60) MaxSeconds = 60;
        StepsChanged();
    }
    public const double Pi4MinuteSeconds = 57.5;

    [RelayCommand]
    void Remove(StepViewModel s)
    {
        Steps.Remove(s);
        StepsChanged();
    }

    [RelayCommand]
    void MoveUp(StepViewModel s)
    {
        int i = Steps.IndexOf(s);
        if (i > 0) { Steps.Move(i, i - 1); StepsChanged(); }
    }

    [RelayCommand]
    void MoveDown(StepViewModel s)
    {
        int i = Steps.IndexOf(s);
        if (i >= 0 && i < Steps.Count - 1) { Steps.Move(i, i + 1); StepsChanged(); }
    }

    // chips from the engine when it is there (the real ones), else empty
    internal static byte[] Encode(TwlProto p, string call, string? loc)
    {
        if (!Engine.Loaded) return Array.Empty<byte>();
        var c = new byte[LibTwist.MaxChips];
        int n = LibTwist.Encode(p, call, loc, c, c.Length);
        return n > 0 ? c[..n] : Array.Empty<byte>();
    }
}

// Settings dialog: the seven tabs of the specification (section 5), on a
// copy of the settings. Save: TryBuild checks every field and returns
// the new settings, or says which field is wrong.
public sealed partial class SettingsViewModel : ObservableObject
{
    static readonly CultureInfo Inv = CultureInfo.InvariantCulture;
    readonly Settings _cfg;

    public SettingsViewModel() : this(new Settings(), Settings.DefaultPath, [], []) { }

    public SettingsViewModel(Settings cfg, string path, IReadOnlyList<string> inputs, IReadOnlyList<string> outputs)
    {
        _cfg = cfg.Clone();                                // edited here; Annuler drops it
        FilePath = path;
        var p = _cfg.Current;
        Profiles = new(_cfg.Profiles.Select(x => x.Name));
        _profile = p.Name;

        _callsign = _cfg.Station.Callsign;
        _locator = _cfg.Station.Locator;
        _knownCalls = _cfg.Station.KnownCalls;
        _locatorLookup = _cfg.Station.LocatorLookup;

        Models = Hamlib.Models().Select(m => m.ToString()).ToArray();
        try { Ports = System.IO.Ports.SerialPort.GetPortNames().OrderBy(x => x.Length).ThenBy(x => x).ToArray(); }
        catch { Ports = []; }
        _inputs = inputs;
        _outputs = outputs;
        _editing = p.Name;
        LoadProfile(p);

        var r = _cfg.Reports;
        _logFolder = r.LogFolder; _csvLog = r.CsvLog; _keepWav = r.KeepWav;
        _wavDays = r.WavDays.ToString(Inv); _wavMaxMB = r.WavMaxMB.ToString(Inv);
        _recordMaxMinutes = r.RecordMaxMinutes.ToString(Inv); _recordMinFreeGB = r.RecordMinFreeGB.ToString("0.#", Inv);
        _fbbOn = r.Fbbspots;
        _fbbKey = "";
        _fbbKeyHint = r.FbbspotsKey == "" ? "fbb_…" : L.T("enregistrée (laisser vide pour la garder)");
        _mqttOn = r.Mqtt; _mqttHost = r.MqttHost; _mqttPort = r.MqttPort.ToString(Inv); _mqttUser = r.MqttUser;
        _mqttTls = r.MqttTls; _mqttTopic = r.MqttTopic;
        _mqttPasswordHint = r.MqttPassword == "" ? "" : L.T("enregistré (laisser vide pour le garder)");

        _language = _cfg.Interface.Language == "en" ? "English" : "Français";   // no-tr (each language in its own name)
        _theme = _cfg.Interface.Theme switch { "light" => "Clair", "system" => "Comme Windows", _ => "Sombre" };
        _spotSound = _cfg.Interface.SpotSound;
        _wfMinuteIndex = Math.Max(0, Array.IndexOf(MinuteValues, _cfg.Interface.Waterfall.MinuteLines));
        _liveFrequency = _cfg.Interface.Waterfall.LiveFrequency;
        _checkUpdates = _cfg.Interface.CheckUpdates;
    }

    public static string DefaultDevice => L.T("Périphérique par défaut du système");
    static string[] Devices(IReadOnlyList<string> found, string saved)
    {
        var l = new List<string> { DefaultDevice };
        l.AddRange(found);
        if (saved != "" && !found.Contains(saved)) l.Add(saved + Absent);
        return l.ToArray();
    }
    static string N(double v) => v.ToString("0.###", Inv);
    static string Absent => " — absent";

    // ---- profiles: the fields of the Audio, Radio, Décodage and Balise
    // tabs belong to the profile chosen at the top ----

    readonly IReadOnlyList<string> _inputs, _outputs;
    string _editing;                                   // name of the profile in the fields
    bool _switching;

    void LoadProfile(ProfileSettings p)
    {
        // "" (the default device) first, then the devices; a saved device
        // that is not there now stays in the list, marked absent
        InputDevices = Devices(_inputs, p.Audio.Input);
        OutputDevices = Devices(_outputs, p.Audio.Output);
        InputDevice = p.Audio.Input == "" ? DefaultDevice : InputDevices.First(d => d.StartsWith(p.Audio.Input));
        OutputDevice = p.Audio.Output == "" ? DefaultDevice : OutputDevices.First(d => d.StartsWith(p.Audio.Output));
        TxLevel = p.Audio.TxLevel;
        ToneHz = N(p.Audio.ToneHz);

        CatMode = p.Radio.Cat switch { "hamlib" => 1, "net" => 2, _ => 0 };
        RigText = p.Radio.RigModel > 0
            ? Models.FirstOrDefault(m => m.EndsWith($"({p.Radio.RigModel})")) ?? $"{p.Radio.Rig} ({p.Radio.RigModel})"
            : "";
        Port = p.Radio.Port;
        Baud = p.Radio.Baud.ToString(Inv);
        NetHost = p.Radio.NetHost;
        NetPort = p.Radio.NetPort.ToString(Inv);
        DataPtt = p.Radio.DataPtt;
        TxDelay = N(p.Radio.TxDelaySeconds);
        PttMethod = Math.Max(0, Array.IndexOf(PttNames, p.Radio.Ptt));
        PttPort = p.Radio.PttPort;
        FreqSource = p.Radio.FreqFromCat ? 0 : 1;
        ManualFreq = MainWindowViewModel.FormatMHz(p.Radio.FrequencyMHz);

        var d = p.Decoding;
        SearchClassic = d.Classic; SearchTwistS = d.TwistS; SearchTwistL = d.TwistL;
        SearchPi4 = d.Pi4; Pi4CenterHz = N(d.Pi4CenterHz); Pi4WidthHz = N(d.Pi4WidthHz);
        CenterHz = N(d.CenterHz); WidthHz = N(d.WidthHz);
        SpreadHz = N(d.SpreadHz); PassPeriod = N(d.PassSeconds);
        ChainDelay = N(d.ChainDelaySeconds);

        MaxTx = p.Beacon.MaxTxSeconds.ToString(Inv);
        TuneStop = p.Beacon.TuneSeconds.ToString(Inv);
    }

    public System.Collections.ObjectModel.ObservableCollection<string> Profiles { get; }
    [ObservableProperty] private string _profile;
    public bool CanDeleteProfile => Profiles.Count > 1;

    // Station
    [ObservableProperty] private string _callsign;
    [ObservableProperty] private string _locator;
    [ObservableProperty] private string _knownCalls;
    [ObservableProperty] private bool _locatorLookup;
    // Audio
    [ObservableProperty] private string[] _inputDevices = [];
    [ObservableProperty] private string[] _outputDevices = [];
    [ObservableProperty] private string _inputDevice;
    [ObservableProperty] private string _outputDevice;
    [ObservableProperty] private double _txLevel;
    [ObservableProperty] private string _toneHz;
    // Radio: CAT by Hamlib's rigctld, PTT
    static readonly string[] PttNames = ["VOX", "CAT", "DTR", "RTS"];
    public string[] Models { get; }
    public string[] Ports { get; }
    public string[] Bauds { get; } = ["1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200"];
    [ObservableProperty] private int _catMode;               // 0 none, 1 rigctld started by TWIST, 2 rigctld on the network
    [ObservableProperty] private string _rigText;
    [ObservableProperty] private string _port;
    [ObservableProperty] private string _baud;
    [ObservableProperty] private string _netHost;
    [ObservableProperty] private string _netPort;
    [ObservableProperty] private bool _dataPtt;
    [ObservableProperty] private string _txDelay;
    [ObservableProperty] private string _radioTest = "";
    [ObservableProperty] private bool _testing;
    public bool HamlibMode => CatMode == 1;
    public bool NetMode => CatMode == 2;
    public bool CatOn => CatMode != 0;
    public string HamlibInfo => Hamlib.Tool("rigctld") is string t
        ? L.F("Hamlib : {0} modèles · {1}", Models.Length, t)
        : L.T("rigctld introuvable : installer Hamlib (Linux : paquet libhamlib-utils) ou utiliser le paquet Windows de TWIST");
    partial void OnCatModeChanged(int value)
    {
        OnPropertyChanged(nameof(HamlibMode)); OnPropertyChanged(nameof(NetMode)); OnPropertyChanged(nameof(CatOn));
        if (value == 0 && FreqSource == 0) FreqSource = 1;
    }

    // the dialog's radio test, done by the main window (it owns the link)
    public Func<RadioSettings, bool, Task<string>>? TestRadio { get; set; }

    [RelayCommand]
    async Task TestCat() => await RunTest(false);
    [RelayCommand]
    async Task TestPtt() => await RunTest(true);

    async Task RunTest(bool ptt)
    {
        if (TestRadio is null || Testing) return;
        var c = TryBuild();
        if (c is null) return;
        Testing = true;
        RadioTest = ptt ? L.T("PTT : 2 s d'émission…") : L.T("connexion…");
        try { RadioTest = await TestRadio(c.Current.Radio, ptt); }
        finally { Testing = false; }
    }

    [ObservableProperty] private bool _fbbOn;
    [ObservableProperty] private string _fbbKey;
    [ObservableProperty] private string _fbbKeyHint;

    // the key asked by email for the callsign and locator of the Station tab
    public string FbbSite => _cfg.Reports.FbbspotsSite is { Length: > 0 } s ? s : Fbbspots.DefaultSite;
    public Task<(bool ok, string message)> RequestKey(string email) =>
        Fbbspots.RegisterAsync(KeyHttp, FbbSite, Callsign.Trim().ToUpperInvariant(), Locator.Trim().ToUpperInvariant(),
                               email.Trim(), L.English ? "en" : "fr");
    public System.Net.Http.HttpClient KeyHttp { get; set; } = new() { Timeout = TimeSpan.FromSeconds(20) };

    // Diagnostic (fbbspots): run by the main window with the sending and
    // the key typed here (an empty key field: the saved key)
    public Action<bool, string?>? Diagnose { get; set; }
    [RelayCommand] void FbbDiagnose() => Diagnose?.Invoke(FbbOn, FbbKey.Trim() == "" ? null : FbbKey.Trim());

    [ObservableProperty] private string _mqttTest = "";
    [RelayCommand]
    async Task TestMqtt()
    {
        var c = TryBuild();
        if (c is null) return;
        MqttTest = L.T("connexion…");
        MqttTest = await Mqtt.TestAsync(c.Reports);
    }
    [ObservableProperty] private int _pttMethod;              // 0 VOX, 1 CAT, 2 DTR, 3 RTS
    [ObservableProperty] private string _pttPort;
    // 0: read by CAT, 1: entered. An int for the two radio buttons: two
    // buttons of a group bound to a bool and its negation loop forever
    // (each change of one rewrites the other)
    [ObservableProperty] private int _freqSource;
    public bool ManualFreqEnabled => FreqSource == 1;
    partial void OnFreqSourceChanged(int value) => OnPropertyChanged(nameof(ManualFreqEnabled));
    [ObservableProperty] private string _manualFreq;
    public bool PttPortEnabled => PttMethod >= 2;
    partial void OnPttMethodChanged(int value) => OnPropertyChanged(nameof(PttPortEnabled));
    // Décodage
    [ObservableProperty] private bool _searchClassic;
    [ObservableProperty] private bool _searchTwistS;
    [ObservableProperty] private bool _searchTwistL;
    [ObservableProperty] private bool _searchPi4;
    [ObservableProperty] private string _pi4CenterHz = "800";
    [ObservableProperty] private string _pi4WidthHz = "150";
    [ObservableProperty] private string _centerHz;
    [ObservableProperty] private string _widthHz;
    [ObservableProperty] private string _spreadHz;
    [ObservableProperty] private string _passPeriod;
    [ObservableProperty] private string _chainDelay;
    public string ClassicLabel => Engine.Label(TwlProto.Classic);
    public bool HasClassic => Engine.HasClassic;
    public bool HasPi4 => Engine.HasPi4;
    // Balise
    [ObservableProperty] private string _maxTx;
    [ObservableProperty] private string _tuneStop;
    // Rapports
    [ObservableProperty] private string _logFolder;
    [ObservableProperty] private bool _csvLog;
    [ObservableProperty] private bool _keepWav;
    [ObservableProperty] private string _wavDays;
    [ObservableProperty] private string _wavMaxMB;
    [ObservableProperty] private string _recordMaxMinutes;
    [ObservableProperty] private string _recordMinFreeGB;
    [ObservableProperty] private bool _mqttOn;
    [ObservableProperty] private string _mqttHost;
    [ObservableProperty] private string _mqttPort;
    [ObservableProperty] private string _mqttUser;
    [ObservableProperty] private bool _mqttTls;
    [ObservableProperty] private string _mqttTopic;
    [ObservableProperty] private string _mqttPassword = "";
    [ObservableProperty] private string _mqttPasswordHint;
    // Interface
    public string[] Languages { get; } = ["Français", "English"];   // no-tr
    public string[] Themes { get; } = ["Sombre", "Clair", "Comme Windows"];
    [ObservableProperty] private string _language;
    [ObservableProperty] private string _theme;
    [ObservableProperty] private bool _spotSound;
    [RelayCommand] void TryChime() => Services.Chime.Play(force: true);
    // waterfall: a dashed line every N minutes; a frame in progress labelled with its frequency
    static readonly int[] MinuteValues = [0, 1, 2, 5, 10, 15];
    public string[] MinuteNames { get; } = [L.T("aucune"), "1 min", "2 min", "5 min", "10 min", "15 min"];
    [ObservableProperty] private int _wfMinuteIndex;
    [ObservableProperty] private bool _liveFrequency;
    [ObservableProperty] private bool _checkUpdates;

    public string FilePath { get; }
    [ObservableProperty] private string _error = "";
    [ObservableProperty] private int _tab = 0;

    // ---- checks ----

    static double? Num(string s) =>
        double.TryParse(s.Trim().Replace(" ", "").Replace(',', '.'), NumberStyles.Float, Inv, out var v) ? v : null;

    // tab index, message
    (int, string)? _bad;
    double Get(string text, double min, double max, int tab, string what)
    {
        if (_bad is null && (Num(text) is not double v || v < min || v > max))
        {
            _bad = (tab, L.F("{0} : nombre de {1} à {2} attendu", what, min.ToString(Inv), max.ToString(Inv)));
            return min;
        }
        return Num(text) ?? min;
    }

    static string Device(string shown) =>
        shown == DefaultDevice ? "" : shown.EndsWith(Absent) ? shown[..^Absent.Length] : shown;

    // the settings as edited, or null with Error and the tab set
    public Settings? TryBuild()
    {
        _bad = null;
        var c = _cfg;
        var cur = c.Profiles.First(x => x.Name == _editing);
        StoreProfile(cur);

        c.Station.Callsign = Callsign.Trim().ToUpperInvariant();
        c.Station.Locator = Locator.Trim().ToUpperInvariant();
        if (c.Station.Callsign != "" && !Regex.IsMatch(c.Station.Callsign, "^[A-Z0-9/]{3,12}$"))
            _bad ??= (0, L.T("Indicatif : lettres, chiffres et / seulement"));
        if (c.Station.Locator != "" && Maidenhead.Center(c.Station.Locator) is null)
            _bad ??= (0, L.T("Locator : 4 ou 6 caractères, par exemple JN12 ou JN12AX"));
        c.Station.KnownCalls = LocatorBook.FormatList(LocatorBook.ParseList(KnownCalls));
        c.Station.LocatorLookup = LocatorLookup;


        var r = c.Reports;
        r.LogFolder = LogFolder.Trim();
        if (r.LogFolder == "") _bad ??= (5, L.T("Dossier du journal : à renseigner"));
        r.CsvLog = CsvLog; r.KeepWav = KeepWav;
        r.WavDays = (int)Get(WavDays, 1, 3650, 5, L.T("WAV conservés : jours"));
        r.WavMaxMB = (int)Get(WavMaxMB, 10, 1000000, 5, L.T("WAV conservés : taille maximale (Mo)"));
        r.RecordMaxMinutes = (int)Get(RecordMaxMinutes, 0, 100000, 5, L.T("Enregistrement continu : durée maximale (min)"));
        r.RecordMinFreeGB = Get(RecordMinFreeGB, 0, 100000, 5, L.T("Enregistrement continu : espace libre minimal (Go)"));
        r.Fbbspots = FbbOn;
        var fk = FbbKey.Trim();
        if (fk != "" && !Fbbspots.IsKey(fk)) _bad ??= (5, L.T("Clé fbbspots : fbb_ suivi de 40 chiffres hexadécimaux, telle que reçue par email"));
        else if (fk != "") r.FbbspotsKey = Secret.Protect(fk);
        if (FbbOn && r.FbbspotsKey == "" && fk == "") _bad ??= (5, L.T("fbbspots : clé à renseigner (Demander une clé…)"));
        r.Mqtt = MqttOn; r.MqttHost = MqttHost.Trim(); r.MqttUser = MqttUser.Trim();
        r.MqttPort = (int)Get(MqttPort, 1, 65535, 5, L.T("Port MQTT"));
        r.MqttTls = MqttTls;
        r.MqttTopic = MqttTopic.Trim();
        if (MqttPassword != "") r.MqttPassword = Secret.Protect(MqttPassword);
        if (MqttOn && r.MqttHost == "") _bad ??= (5, L.T("MQTT : serveur à renseigner"));
        if (MqttOn && r.MqttTopic == "") _bad ??= (5, L.T("MQTT : topic à renseigner"));

        c.Interface.Language = Language == "English" ? "en" : "fr";
        c.Interface.Theme = Theme switch { "Clair" => "light", "Comme Windows" => "system", _ => "dark" };
        c.Interface.SpotSound = SpotSound;
        c.Interface.Waterfall.MinuteLines = MinuteValues[Math.Clamp(WfMinuteIndex, 0, MinuteValues.Length - 1)];
        c.Interface.Waterfall.LiveFrequency = LiveFrequency;
        c.Interface.CheckUpdates = CheckUpdates;

        if (_bad is { } b) { Tab = b.Item1; Error = b.Item2; return null; }
        Error = "";
        c.Profile = cur.Name;
        return c.Clone();
    }

    // the fields of the profile tabs into p (checked: _bad)
    void StoreProfile(ProfileSettings p)
    {
        p.Audio.Input = Device(InputDevice);
        p.Audio.Output = Device(OutputDevice);
        p.Audio.TxLevel = Math.Round(TxLevel);
        p.Audio.ToneHz = Get(ToneHz, 300, 3000, 1, L.T("Ton d'émission"));

        p.Radio.Cat = CatMode switch { 1 => "hamlib", 2 => "net", _ => "none" };
        var mm = Regex.Match(RigText.Trim(), @"\((\d+)\)$|^(\d+)$");
        p.Radio.RigModel = mm.Success ? int.Parse(mm.Groups[1].Success ? mm.Groups[1].Value : mm.Groups[2].Value, Inv) : 0;
        p.Radio.Rig = Hamlib.Models().FirstOrDefault(x => x.Number == p.Radio.RigModel)?.ToString().Replace($" ({p.Radio.RigModel})", "") ?? "";
        if (CatMode == 1 && p.Radio.RigModel <= 0) _bad ??= (2, L.T("Transceiver : choisir un modèle dans la liste Hamlib"));
        if (CatMode == 1 && Port.Trim() == "") _bad ??= (2, L.T("Port série du CAT à renseigner"));
        p.Radio.Port = Port.Trim();
        p.Radio.Baud = (int)Get(Baud, 300, 1000000, 2, L.T("Vitesse du port"));
        p.Radio.NetHost = NetHost.Trim() == "" ? "127.0.0.1" : NetHost.Trim();
        p.Radio.NetPort = (int)Get(NetPort, 1, 65535, 2, L.T("Port de rigctld"));
        p.Radio.DataPtt = DataPtt;
        p.Radio.TxDelaySeconds = Get(TxDelay, 0, 2, 2, L.T("Avance du PTT"));
        p.Radio.Ptt = PttNames[Math.Clamp(PttMethod, 0, 3)];
        p.Radio.PttPort = PttPort.Trim();
        p.Radio.FreqFromCat = FreqSource == 0 && CatMode != 0;
        if (p.Radio.Ptt == "CAT" && CatMode == 0) _bad ??= (2, L.T("PTT par CAT : choisir d'abord une liaison CAT (Hamlib)"));
        if ((p.Radio.Ptt == "DTR" || p.Radio.Ptt == "RTS") && CatMode != 1 && p.Radio.PttPort == "")
            _bad ??= (2, L.F("PTT {0} : port série à renseigner", p.Radio.Ptt));
        p.Radio.FrequencyMHz = Get(ManualFreq, 0, 300000, 2, L.T("Fréquence de la radio (MHz)"));

        var d = p.Decoding;
        d.Classic = SearchClassic; d.TwistS = SearchTwistS; d.TwistL = SearchTwistL; d.Pi4 = SearchPi4;
        d.CenterHz = Get(CenterHz, 100, 5000, 3, L.T("Centre"));
        d.Pi4CenterHz = Get(Pi4CenterHz, 300, 4000, 3, L.T("PI4 : porteuse"));
        d.Pi4WidthHz = Get(Pi4WidthHz, 10, 1000, 3, L.T("PI4 : largeur"));
        d.WidthHz = Get(WidthHz, 10, 2000, 3, L.T("Largeur"));
        d.SpreadHz = Get(SpreadHz, 0, 100, 3, L.T("Étalement"));
        d.PassSeconds = Get(PassPeriod, 1, 15, 3, L.T("Période des passes"));
        d.ChainDelaySeconds = Get(ChainDelay, -5, 10, 3, L.T("Retard de la chaîne de réception"));

        p.Beacon.MaxTxSeconds = (int)Get(MaxTx, 10, 3600, 4, L.T("Durée maximale d'émission"));
        p.Beacon.TuneSeconds = (int)Get(TuneStop, 1, 600, 4, L.T("Arrêt du Tune"));
    }

    // another profile chosen at the top: the fields go back to the one
    // being edited (if they are valid), then show the chosen one
    partial void OnProfileChanged(string value)
    {
        if (_switching || value is null || value == _editing) return;
        if (!Leave())
        {
            _switching = true;
            Profile = _editing;                       // back to the profile being edited
            _switching = false;
            // and once more after the ComboBox has finished its own change
            Avalonia.Threading.Dispatcher.UIThread.Post(() => OnPropertyChanged(nameof(Profile)));
            return;
        }
        Enter(_cfg.Profiles.First(x => x.Name == value));
    }

    bool Leave()
    {
        _bad = null;
        var cur = _cfg.Profiles.First(x => x.Name == _editing);
        StoreProfile(cur);
        if (_bad is { } b) { Tab = b.Item1; Error = b.Item2; return false; }
        Error = "";
        return true;
    }

    void Enter(ProfileSettings p)
    {
        _editing = p.Name;
        LoadProfile(p);
        _switching = true;
        Profile = p.Name;
        _switching = false;
        OnPropertyChanged(nameof(CanDeleteProfile));
        OnPropertyChanged(nameof(CurrentProfileName));
    }

    // Profile operations, asked by the window (it shows the small
    // dialogs). Each returns null, or why it cannot be done.

    public string CurrentProfileName => _editing;

    string? CheckName(string name, string? except)
    {
        name = name.Trim();
        if (name == "") return L.T("un nom est nécessaire");
        if (_cfg.Profiles.Any(x => x.Name == name && x.Name != except)) return L.F("« {0} » existe déjà", name);
        return null;
    }

    // a new profile, a copy of the current one or the defaults, then shown
    public string? NewProfile(string name, bool copyCurrent)
    {
        if (CheckName(name, null) is string why) return why;
        if (!Leave()) return Error;
        var p = copyCurrent ? Settings.CloneOf(_cfg.Profiles.First(x => x.Name == _editing)) : new ProfileSettings();
        p.Name = name.Trim();
        _cfg.Profiles.Add(p);
        Profiles.Add(p.Name);
        Enter(p);
        return null;
    }

    public string? RenameProfile(string name)
    {
        name = name.Trim();
        if (name == _editing) return null;
        if (CheckName(name, _editing) is string why) return why;
        var p = _cfg.Profiles.First(x => x.Name == _editing);
        int i = Profiles.IndexOf(p.Name);
        p.Name = name;
        _editing = name;
        _switching = true;
        Profiles[i] = name;
        Profile = name;
        _switching = false;
        return null;
    }

    public void DeleteProfile()
    {
        if (_cfg.Profiles.Count < 2) return;
        var cur = _cfg.Profiles.First(x => x.Name == _editing);
        _cfg.Profiles.Remove(cur);
        _switching = true;
        Profiles.Remove(cur.Name);
        _switching = false;
        Enter(_cfg.Profiles[0]);
    }
}

// Tools: chips of a frame (computed by the engine), WAV test file.
public sealed partial class ToolsViewModel : ObservableObject
{
    public string[] FrameTypes { get; } = Engine.HasClassic ? ["TWIST L", "TWIST S", Engine.Label(TwlProto.Classic)] : ["TWIST L", "TWIST S"];
    [ObservableProperty] private string _frameType = "TWIST L";
    [ObservableProperty] private string _call = "F6FBB";
    [ObservableProperty] private string _loc = "JN12AX";
    [ObservableProperty] private byte[] _chips = Array.Empty<byte>();
    [ObservableProperty] private string _bits = "";
    [ObservableProperty] private string _hex = "";
    [ObservableProperty] private string _info = "";

    public ToolsViewModel() { Compute(); WavDescribe(); }

    partial void OnFrameTypeChanged(string value) => Compute();
    partial void OnCallChanged(string value) => Compute();
    partial void OnLocChanged(string value) => Compute();

    TwlProto Proto => FrameType switch { "TWIST L" => TwlProto.TwistL, "TWIST S" => TwlProto.TwistS, _ => TwlProto.Classic };

    void Compute()
    {
        if (!Engine.Loaded) { Info = L.T("bibliothèques absentes : chips indisponibles"); Chips = Array.Empty<byte>(); Bits = Hex = ""; return; }
        var c = BaliseViewModel.Encode(Proto, Call.Trim(), Proto == TwlProto.TwistL ? Loc.Trim() : null);
        Chips = c;
        if (c.Length == 0) { Info = L.T("Indicatif ou locator impossible à coder"); Bits = Hex = ""; return; }
        Bits = string.Concat(c.Select(b => b != 0 ? '1' : '0'));
        var sb = new System.Text.StringBuilder();
        for (int i = 0; i < c.Length; i += 8)
        {
            int v = 0;
            for (int j = 0; j < 8; j++) v = (v << 1) | (i + j < c.Length ? c[i + j] : 0);
            sb.Append(v.ToString("X2"));
        }
        Hex = sb.ToString();
        Info = L.F("{0} chips · {1} s en Op05 · calculés par {2} {3}", c.Length, (c.Length * 0.128).ToString("0.0", L.Culture),
                   Proto == TwlProto.Classic ? "libopera" : "libtwist", Engine.Version);
    }

    // ---- WAV de test (Services/TestWav) ----

    public string[] WavProtos { get; } = new[] { "TWIST L", "TWIST S" }
        .Concat(Engine.HasClassic ? new[] { Engine.Label(TwlProto.Classic) } : Array.Empty<string>())
        .Concat(Engine.HasPi4 ? new[] { "PI4" } : Array.Empty<string>()).ToArray();
    [ObservableProperty] private string _wavProto = "TWIST L";
    [ObservableProperty] private string _wavCall = "F6FBB";
    [ObservableProperty] private string _wavLoc = "JN12AX";
    public string[] WavPi4Messages { get; } = [L.T("indicatif"), "/locator"];
    [ObservableProperty] private int _wavPi4Message;
    [ObservableProperty] private double _wavFreq = 1500;
    [ObservableProperty] private bool _wavNoise = true;
    [ObservableProperty] private double _wavSnr = -15;
    [ObservableProperty] private double _wavBefore = 1;
    [ObservableProperty] private double _wavAfter = 2;
    public int[] WavRates { get; } = [8000, 11025, 12000, 22050, 44100, 48000];
    [ObservableProperty] private int _wavRate = 12000;
    [ObservableProperty] private string _wavInfo = "";
    [ObservableProperty] private string _wavResult = "";
    [ObservableProperty] private bool _wavBusy;

    TwlProto WavP => WavProto switch { "TWIST L" => TwlProto.TwistL, "TWIST S" => TwlProto.TwistS, "PI4" => TwlProto.Pi4, _ => TwlProto.Classic };
    public bool WavIsPi4 => WavP == TwlProto.Pi4;
    public bool WavNeedsLoc => WavP == TwlProto.TwistL || (WavIsPi4 && WavPi4Message == 1);

    partial void OnWavProtoChanged(string value)
    {
        // PI4 has its own zone (800 Hz), the OOK frames 1500 Hz
        if (WavIsPi4 && WavFreq == 1500) WavFreq = 800;
        else if (!WavIsPi4 && WavFreq == 800) WavFreq = 1500;
        OnPropertyChanged(nameof(WavIsPi4));
        OnPropertyChanged(nameof(WavNeedsLoc));
        WavDescribe();
    }
    partial void OnWavPi4MessageChanged(int value) { OnPropertyChanged(nameof(WavNeedsLoc)); WavDescribe(); }
    partial void OnWavCallChanged(string value) => WavDescribe();
    partial void OnWavLocChanged(string value) => WavDescribe();
    partial void OnWavFreqChanged(double value) => WavDescribe();
    partial void OnWavNoiseChanged(bool value) => WavDescribe();
    partial void OnWavSnrChanged(double value) => WavDescribe();
    partial void OnWavBeforeChanged(double value) => WavDescribe();
    partial void OnWavAfterChanged(double value) => WavDescribe();
    partial void OnWavRateChanged(int value) => WavDescribe();

    public Services.TestWav.Request WavRequest() => new(WavP,
        WavIsPi4 && WavPi4Message == 1 ? "" : WavCall.Trim().ToUpperInvariant(),
        WavLoc.Trim(), Math.Clamp(WavFreq, 200, 0.45 * WavRate - (WavIsPi4 ? 600 : 0)),
        WavNoise ? Math.Clamp(WavSnr, -40, 40) : null, Math.Clamp(WavBefore, 0, 60), Math.Clamp(WavAfter, 0, 60), WavRate);

    // what the file will hold, and its length
    public void WavDescribe()
    {
        if (!Engine.Loaded) { WavInfo = L.T("bibliothèques absentes"); return; }
        var r = WavRequest();
        double d = Services.TestWav.FrameSeconds(r);
        WavInfo = d < 0
            ? (WavIsPi4 ? L.T("message PI4 impossible : 8 caractères au plus (A-Z 0-9 /)") : L.T("Indicatif ou locator impossible à coder"))
            : L.F("trame de {0} s, fichier de {1} s", d.ToString("0.0", L.Culture), (r.BeforeS + d + r.AfterS).ToString("0.0", L.Culture));
    }

    // writes the file, then decodes it back (in the background)
    public async Task WavWrite(string path)
    {
        var r = WavRequest();
        WavBusy = true;
        WavResult = L.T("génération…");
        try
        {
            var (y, spots) = await Task.Run(() =>
            {
                var y = Services.TestWav.Make(r);
                Services.Wav.WriteMono16(path, y, r.Rate, "TWIST Console test: " + Services.TestWav.Describe(r));
                return (y, Services.TestWav.Check(y, r));
            });
            string found = spots.Length == 0 ? L.T("pas décodé (SNR trop bas ?)")
                : string.Join(", ", spots.Select(p => $"{Engine.Label(p.Proto)} {(p.Call == "" ? "/" + p.Loc : p.Call)}{(p.Call != "" && p.Loc != "" ? " " + p.Loc : "")} "
                                                    + $"{p.Snr:0} dB {p.Freq:0} Hz {p.Start:0.0} s"));
            WavResult = L.F("{0} : {1} s — relu par le décodeur : {2}", Path.GetFileName(path), (y.Length / (double)r.Rate).ToString("0.0", L.Culture), found);
        }
        catch (Exception e) { WavResult = L.T("WAV non écrit : ") + e.Message; }
        finally { WavBusy = false; }
    }
}
