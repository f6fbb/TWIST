using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Twist.App.Services;
using Twist.Native;
using Twist.App.Lang;

namespace Twist.App.ViewModels;

// Transmission (docs/SPECIFICATION.md, section 3): Tune, a sequence sent
// once, the beacon cycle, the watchdog and the immediate stop. The
// transmitter (Services/Beacon.cs) is polled ten times a second by the
// window's fast timer (TxTick). PTT: VOX only until step 6.
public partial class MainWindowViewModel
{
    Beacon? _beacon;

    [ObservableProperty] private bool _txActive;
    [ObservableProperty] private bool _txWaiting;              // lead pause before the second of the cycle
    [ObservableProperty] private string _txTitle = "";
    [ObservableProperty] private string _txTimeText = "";
    [ObservableProperty] private string _txStepText = "";
    [ObservableProperty] private double _txProgress;
    [ObservableProperty] private byte[] _txChips = [];
    [ObservableProperty] private int _txSent;
    [ObservableProperty] private string _txPttText = "";
    [ObservableProperty] private string _txInfoText = "";
    [ObservableProperty] private bool _tuneActive;
    [ObservableProperty] private string _tuneLabel = "Tune";
    [ObservableProperty] private bool _cycleRunning;
    [ObservableProperty] private double _txLevel = 50;          // % of full scale
    [ObservableProperty] private string _badgeText = L.T("balise arrêtée");
    // the badge's colours: Border.badge.cycle / .tx (App.axaml)
    [ObservableProperty] private bool _badgeCycle;
    [ObservableProperty] private bool _badgeTx;

    DateTime _nextCycle = DateTime.MaxValue;
    bool _cyclePending;            // the second came during a Tune: sent when it ends
    bool _decodingOffForCycle;

    public string TxLevelText => TxLevel <= 0 ? "—" : L.Dec($"{20 * Math.Log10(TxLevel / 100):0.0} dBFS").Replace("-", "−");
    public int TuneSeconds => _cfg.Current.Beacon.TuneSeconds;

    void InitTx()
    {
        TxLevel = _cfg.Current.Audio.TxLevel;
        UpdateTuneLabel();
        if (!Engine.Loaded) return;
        try
        {
            _beacon = new Beacon();
            _beacon.Message += m => Log(m, warning: true);
        }
        catch (Exception e) { Log(L.T("émission impossible : ") + e.Message, warning: true); }
    }

    partial void OnTxLevelChanged(double value)
    {
        _cfg.Current.Audio.TxLevel = Math.Round(value);
        _beacon?.SetLevel(value / 100);                        // heard at once during a Tune
        OnPropertyChanged(nameof(TxLevelText));
        if (!_loading) { _saveTimer.Stop(); _saveTimer.Start(); }
    }

    // the window's settings changed under it (dialog): level, tune length
    void TxSettingsChanged()
    {
        TxLevel = _cfg.Current.Audio.TxLevel;
        UpdateTuneLabel();
    }

    // a change in the beacon window: saved two seconds later
    public void SaveSoon()
    {
        _saveTimer.Stop();
        _saveTimer.Start();
        UpdateTuneLabel();
        if (CycleRunning) UpdateNext();
    }

    void UpdateTuneLabel() =>
        TuneLabel = TuneActive ? L.T("Arrêter le Tune") : $"Tune · {TuneSeconds} s";

    // ---- what is sent ----

    // why nothing can be sent now, or null
    string? CannotSend(bool frames)
    {
        if (_beacon is null) return L.T("émission indisponible (bibliothèques absentes)");
        if (PttProblem() is string p) return p;
        if (frames && Callsign == "") return L.T("indicatif à régler : Fichier › Réglages › Station");
        return null;
    }

    TxOutput Output()
    {
        var (dev, rate) = Beacon.FindOutput(_cfg.Current.Audio.Output);
        var a = _cfg.Current.Audio;
        return new TxOutput(dev, rate, a.ToneHz, TxLevel / 100, _cfg.Current.Beacon.MaxTxSeconds);
    }

    // the sequence of the settings, with the station's callsign and locator;
    // cycleStart: when it goes out (a PI4 frame alternating its callsign
    // and locator sends the one of that cycle)
    public TwlStep[] SequenceSteps(out string? problem, DateTime? cycleStart = null)
    {
        problem = null;
        var list = new List<TwlStep>();
        foreach (var s in _cfg.Current.Beacon.Steps)
        {
            list.Add(StepOf(s, cycleStart ?? DateTime.UtcNow, out var p));
            problem ??= p;
        }
        if (list.Count == 0) problem ??= L.T("séquence vide");
        return list.ToArray();
    }

    // one step of the settings, for the cycle starting at `start`
    public TwlStep StepOf(StepSettings s, DateTime start, out string? problem)
    {
        problem = null;
        var b = _cfg.Current.Beacon;
        double pi4Hz = b.Pi4ToneHz;
        switch (s.Kind)
        {
            case "carrier":
                var c = TwlStep.Carrier(Math.Clamp(s.Seconds, 0.1, 600));
                if (s.AtPi4) c.ToneHz = pi4Hz;
                return c;
            case "pause":
                return TwlStep.Pause(Math.Clamp(s.Seconds, 0.1, 3600), s.PttHold);
            case "cw":
                var w = TwlStep.Cw(Callsign, Locator, b.CwWpm, s.AtPi4 ? pi4Hz : 0);
                if (Callsign != "" && LibTwist.StepSeconds(w) < 0) problem = L.F("CW : « {0} {1} » impossible à émettre (A-Z 0-9 /)", Callsign, Locator);
                return w;
        }
        var p = ProtoOf(s.Proto);
        if (p == TwlProto.Pi4)
        {
            bool loc = Pi4SendsLocator(s.Pi4Message, start);
            if (loc && Locator == "") problem = L.T("PI4 /locator : locator à régler (Réglages › Station)");
            var f = loc ? TwlStep.Pi4("", Locator, pi4Hz) : TwlStep.Pi4(Callsign, "", pi4Hz);
            if (Callsign != "" && problem is null && LibTwist.StepSeconds(f) < 0)
                problem = L.F("PI4 : message « {0} » impossible (8 caractères au plus : A-Z 0-9 /)", loc ? "/" + Locator : Callsign);
            return f;
        }
        if (p == TwlProto.TwistL && Locator == "") problem = L.T("TWIST L : locator à régler (Réglages › Station)");
        return TwlStep.Frame(p, Callsign, p == TwlProto.TwistL ? Locator : "");
    }

    // a PI4 frame of the cycle starting at `start`: "/locator" rather than
    // the callsign? alt: every other cycle (counted from 1970, the
    // callsign on even ones)
    public bool Pi4SendsLocator(string message, DateTime start)
    {
        if (message == "loc") return true;
        if (message != "alt") return false;
        int period = Math.Clamp(_cfg.Current.Beacon.PeriodMinutes, 1, 1440);
        long k = (long)Math.Floor((start - DateTime.UnixEpoch).TotalMinutes / period + 1e-6);
        return k % 2 == 1;
    }

    public static TwlProto ProtoOf(string name) => name switch
    {
        "classic" => TwlProto.Classic,
        "twists" => TwlProto.TwistS,
        "pi4" => TwlProto.Pi4,
        _ => TwlProto.TwistL,
    };

    string SequenceTitle(TwlStep[] steps) =>
        string.Join(" + ", steps.Select(s => s.Kind switch
        {
            TwlStepKind.Frame when s.Proto == TwlProto.Pi4 => "PI4 " + (s.Call == "" ? "/" + s.Loc : s.Call),
            TwlStepKind.Frame => Engine.Label(s.Proto),
            TwlStepKind.Cw => "CW",
            TwlStepKind.Carrier => L.F("porteuse {0:0.#} s", s.Seconds),
            _ => L.F("pause {0:0.#} s", s.Seconds),
        })) + $" · {Callsign}" + (steps.Any(s => s.Kind == TwlStepKind.Frame && s.Proto == TwlProto.TwistL) ? " " + Locator : "");

    bool Send(TwlStep[] steps, string title, bool tune, DateTime? at = null)
    {
        try
        {
            // PTT by the radio: the audio starts TX delay after the PTT
            // (DrivePtt keys it as soon as the start is that close)
            var r = _cfg.Current.Radio;
            if (r.Ptt != "VOX" && at is null) at = DateTime.UtcNow.AddSeconds(Math.Clamp(r.TxDelaySeconds, 0.1, 2));
            _beacon!.Start(steps, Output(), title, tune, at);
            Log((tune ? "Tune : " : L.T("émission : ")) + title + (at is DateTime t && (t - DateTime.UtcNow).TotalSeconds > 0.05 ? $" à {t:HH:mm:ss}" : ""));
            TxTick();
            return true;
        }
        catch (Exception e)
        {
            Log(L.T("émission refusée : ") + e.Message, warning: true);
            return false;
        }
    }

    // ---- commands ----

    [RelayCommand]
    public void ToggleTune()
    {
        if (TuneActive) { _beacon?.Stop(); return; }
        if (CannotSend(false) is string why) { Log(L.T("Tune refusé : ") + why, warning: true); return; }
        if (_beacon!.Active) { Log(L.T("Tune refusé : émission en cours"), warning: true); return; }
        int sec = Math.Clamp(TuneSeconds, 1, 600);
        int max = _cfg.Current.Beacon.MaxTxSeconds;
        if (sec > max) { Log(L.F("Tune limité à {0} s par le chien de garde", max)); sec = max; }
        Send([TwlStep.Carrier(sec)], L.F("porteuse {0:0} Hz, arrêt après {1} s", _cfg.Current.Audio.ToneHz, sec), tune: true);
    }

    [RelayCommand]
    public void SendNow()
    {
        if (CannotSend(true) is string why) { Log(L.T("émission refusée : ") + why, warning: true); return; }
        if (_beacon!.Active) { Log(L.T("émission refusée : émission en cours"), warning: true); return; }
        var steps = SequenceSteps(out var problem);
        if (problem is not null) { Log(L.T("émission refusée : ") + problem, warning: true); return; }
        Send(steps, SequenceTitle(steps), tune: false);
    }

    [RelayCommand]
    public void StartCycle()
    {
        if (CannotSend(true) is string why) { Log(L.T("cycle refusé : ") + why, warning: true); return; }
        var steps = SequenceSteps(out var problem);
        if (problem is not null) { Log(L.T("cycle refusé : ") + problem, warning: true); return; }
        var b = _cfg.Current.Beacon;
        double total = Beacon.TotalSeconds(steps);
        if (total >= b.PeriodMinutes * 60 - 2)
        {
            Log(L.F("cycle refusé : séquence de {0:0} s, plus longue que la période ({1} min)", total, b.PeriodMinutes), warning: true);
            return;
        }
        double longest = Beacon.LongestPtt(steps);
        if (longest > b.MaxTxSeconds)
        {
            Log(L.F("cycle refusé : PTT maintenu {0:0} s, plus que le chien de garde ({1} s)", longest, b.MaxTxSeconds), warning: true);
            return;
        }
        CycleRunning = true;
        _cyclePending = false;
        UpdateNext();
        Log(L.F("cycle démarré : toutes les {0} min à la seconde {1}, première émission {2:HH:mm:ss}", b.PeriodMinutes, b.StartSecond, _nextCycle));
        if (!b.ReceiveBetween && DecodingActive) { _decodingOffForCycle = true; DecodingActive = false; }
        UpdateBadge();
    }

    [RelayCommand]
    public void StopCycle()
    {
        if (!CycleRunning) return;
        CycleRunning = false;
        _cyclePending = false;
        _nextCycle = DateTime.MaxValue;
        Log(L.T("cycle arrêté"));
        if (_decodingOffForCycle) { _decodingOffForCycle = false; DecodingActive = true; }
        UpdateBadge();
    }

    // the red button and Escape: the transmission stops at once, and the
    // cycle with it (nothing goes out again without asking)
    [RelayCommand]
    public void StopTx()
    {
        bool any = _beacon?.Active == true || CycleRunning;
        _beacon?.Stop();
        ReleasePtt();
        StopCycle();
        if (any) Log(L.T("arrêt immédiat"));
    }

    void UpdateNext()
    {
        var b = _cfg.Current.Beacon;
        // the next second that still leaves time to prepare (1 s)
        _nextCycle = Beacon.NextStart(DateTime.UtcNow.AddSeconds(1), b.PeriodMinutes, b.StartSecond);
    }

    // ---- ten times a second ----

    public void TxTick()            // public: the screenshot tool drives it (headless timers do not run)
    {
        if (_beacon is null) return;
        var now = DateTime.UtcNow;

        // cycle: start 1 s ahead, the lead pause ends on the second
        if (CycleRunning && !_beacon.Active)
        {
            if (_cyclePending)
            {
                _cyclePending = false;
                var steps = SequenceSteps(out _, now);
                Send(steps, SequenceTitle(steps) + L.T(" (après le Tune)"), tune: false);
            }
            else if (now >= _nextCycle.AddSeconds(-1))
            {
                var at = _nextCycle;
                UpdateNext();
                var steps = SequenceSteps(out var problem, at);
                if (problem is not null) { Log("cycle : " + problem, warning: true); StopCycle(); }
                else if (!Send(steps, SequenceTitle(steps), tune: false, at)) StopCycle();
            }
        }
        else if (CycleRunning && _beacon.Active && _beacon.IsTune && now >= _nextCycle)
        {
            _cyclePending = true;                       // waits for the end of the Tune
            UpdateNext();
            Log(L.T("émission du cycle en attente de la fin du Tune"));
        }

        var st = _beacon.Poll();
        DrivePtt();
        bool was = TxActive;
        TxActive = st.Active && !st.Waiting;
        TxWaiting = st.Active && st.Waiting;
        TuneActive = st.Active && _beacon.IsTune;
        UpdateTuneLabel();
        if (st.Active)
        {
            TxTitle = (_beacon.IsTune ? "TUNE · " : "") + _beacon.Title;
            var o = _beacon.Output!;
            TxTimeText = st.Waiting ? L.F("départ dans {0:0.0} s", -st.Elapsed)
                                    : L.Dec($"{Math.Max(0, st.Elapsed):0.0} s / {st.Total:0.0} s");
            TxProgress = st.Total > 0 ? Math.Clamp(100 * st.Elapsed / st.Total, 0, 100) : 0;
            var chips = st.Step >= 0 && st.Step < _beacon.Chips.Length ? _beacon.Chips[st.Step] : null;
            TxChips = chips ?? [];
            TxSent = chips is not null && st.Chip >= 0 ? st.Chip : 0;
            TxStepText = st.Waiting ? L.T("attente de la seconde du cycle") : st.Kind switch
            {
                TwlStepKind.Frame when st.NChips == 146 && StepProto(st.Step) == TwlProto.Pi4
                    => L.F("étape {0} · PI4 · symbole {1} / {2}", st.Step + 1, st.Chip + 1, st.NChips),
                TwlStepKind.Frame => L.F("étape {0} · trame · chip {1} / {2}", st.Step + 1, st.Chip + 1, st.NChips),
                TwlStepKind.Cw => L.F("étape {0} · CW", st.Step + 1),
                TwlStepKind.Carrier => L.F("étape {0} · porteuse", st.Step + 1),
                _ => L.F("étape {0} · pause", st.Step + 1),
            };
            var rr = _cfg.Current.Radio;
            TxPttText = rr.Ptt == "VOX" ? "PTT : VOX — " + (st.Ptt ? L.T("émission") : L.T("relâché"))
                                        : $"PTT : {rr.Ptt} — " + (_pttKeyed ? L.T("actif") : L.T("relâché"));
            // (the frequency of the step on the air: PI4 and the steps at the PI4 frequency have their own)
            double hz = st.Step >= 0 && st.Step < _beacon.Steps.Length && _beacon.Steps[st.Step].ToneHz > 0 ? _beacon.Steps[st.Step].ToneHz : o.ToneHz;
            TxInfoText = L.F("Ton {0:0} Hz · niveau {1} · chien de garde {2:0} / {3:0} s", hz, TxLevelText, st.PttSeconds, o.MaxTxSeconds);
        }
        else if (was) Log(L.T("fin d'émission"));
        UpdateBadge();
        NextTx = st.Active && !st.Waiting ? L.T("émission en cours")
               : CycleRunning ? L.F("{0:HH:mm:ss} (dans {1})", _nextCycle, Until(_nextCycle - now))
               : L.T("cycle arrêté");
        NextTxShown = CycleRunning && !(st.Active && !st.Waiting);
    }

    TwlProto StepProto(int i) => _beacon is not null && i >= 0 && i < _beacon.Steps.Length ? _beacon.Steps[i].Proto : 0;

    static string Until(TimeSpan t) =>
        t.TotalSeconds < 0 ? "0 s" : t.TotalMinutes >= 1 ? $"{(int)t.TotalMinutes} min {t.Seconds:00} s" : $"{t.Seconds} s";

    void UpdateBadge()
    {
        BadgeTx = TxActive;
        BadgeCycle = !TxActive && CycleRunning;
        BadgeText = TxActive ? (TuneActive ? "TUNE" : L.T("ÉMISSION")) : CycleRunning ? L.T("balise en cycle") : L.T("balise arrêtée");
    }
}
