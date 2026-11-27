using CommunityToolkit.Mvvm.Input;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using Avalonia.Threading;
using Twist.App.Services;
using Twist.App.Lang;

namespace Twist.App.ViewModels;

// The radio (Hamlib's rigctld: frequency, PTT) and the MQTT publication
// of the spots (spyfbbclient's format). Both are (re)opened in the
// background when the settings change, and reported in the journal and
// the status bar.
public partial class MainWindowViewModel
{
    Rig? _rig;
    Mqtt? _mqtt;
    FbbspotsSender? _fbb;
    string _rigKey = "", _mqttKey = "", _fbbKey = "";
    int _rigGeneration;

    [ObservableProperty] private string _footerCat = "";
    public bool FreqFromCat => _cfg.Current.Radio.FreqFromCat && _cfg.Current.Radio.Cat != "none";
    public bool FreqEditable => !FreqFromCat;

    static string Key(object o) => JsonSerializer.Serialize(o);

    // radio and broker as the settings say; nothing done if unchanged
    void OpenLinks(bool force = false)
    {
        var r = _cfg.Current.Radio;
        bool needRig = r.Cat != "none" || r.Ptt == "DTR" || r.Ptt == "RTS";
        var key = needRig ? Key(LinkFields(r)) : "";
        if (force || key != _rigKey)
        {
            _rigKey = key;
            var old = _rig;
            _rig = null;
            int gen = ++_rigGeneration;
            FooterCat = needRig ? L.T("CAT : connexion…") : L.T("CAT : aucun");
            OnPropertyChanged(nameof(FreqFromCat));
            OnPropertyChanged(nameof(FreqEditable));
            var copy = Settings.CloneOf(r);
            Task.Run(() =>
            {
                try { old?.Dispose(); } catch { }
                if (!needRig) return;
                var rig = new Rig(copy);
                rig.Message += (m, warn) => Dispatcher.UIThread.Post(() => Log(m, warning: warn));
                try
                {
                    rig.Open();
                    Dispatcher.UIThread.Post(() =>
                    {
                        if (gen != _rigGeneration) { Task.Run(rig.Dispose); return; }
                        _rig = rig;
                        UpdateCat();
                    });
                }
                catch (Exception e)
                {
                    rig.Dispose();
                    Dispatcher.UIThread.Post(() =>
                    {
                        if (gen != _rigGeneration) return;
                        Log($"CAT / PTT : {e.Message}", warning: true);
                        FooterCat = L.T("CAT : hors service");
                    });
                }
            });
        }

        var m = _cfg.Reports;
        var mkey = m.Mqtt && m.MqttHost.Trim() != "" ? Key(m) : "";
        if (force || mkey != _mqttKey)
        {
            _mqttKey = mkey;
            var old = _mqtt;
            _mqtt = null;
            Task.Run(() => old?.Dispose());
            if (mkey != "")
            {
                _mqtt = new Mqtt(Settings.CloneOf(m));
                _mqtt.Message += (x, warn) => Dispatcher.UIThread.Post(() => Log(x, warning: warn));
            }
        }

        var fkey = m.Fbbspots ? Secret.Unprotect(m.FbbspotsKey).Trim() : "";
        var fid = fkey == "" ? "" : m.FbbspotsSite + " " + fkey;
        if (force || fid != _fbbKey)
        {
            _fbbKey = fid;
            var old = _fbb;
            _fbb = null;
            Task.Run(() => old?.Dispose());
            if (m.Fbbspots && fkey == "") Log(L.T("fbbspots : pas de clé (Réglages › Rapports › Demander une clé…)"), warning: true);
            if (fid != "")
            {
                _fbb = new FbbspotsSender(m.FbbspotsSite, fkey);
                _fbb.Message += (x, warn) => Dispatcher.UIThread.Post(() => Log(x, warning: warn));
            }
        }
    }

    // only the fields that change the link (not the frequency typed in)
    static object LinkFields(RadioSettings r) =>
        new { r.Cat, r.RigModel, r.Port, r.Baud, r.NetHost, r.NetPort, r.Ptt, r.PttPort, r.DataPtt };

    // once a second: the dial frequency from the radio
    void UpdateCat()
    {
        var r = _cfg.Current.Radio;
        if (_rig is null)
        {
            if (_rigKey == "") FooterCat = r.Ptt == "VOX" ? L.T("CAT : aucun · PTT VOX") : L.T("CAT : aucun");
            return;
        }
        if (_rig.Connected && _rig.FrequencyHz is double hz && FreqFromCat)
        {
            var text = FormatMHz(hz / 1e6);
            if (text != RadioFrequency) RadioFrequency = text;
        }
        FooterCat = (r.Cat == "none" ? $"PTT {r.Ptt} : {r.PttPort}"
                     : $"CAT : {_rig.Description}" + (_rig.Connected ? "" : L.T(" — pas de réponse")))
                    + (r.Ptt != "VOX" && r.Cat != "none" ? $" · PTT {r.Ptt}" : "")
                    + (_rig.PttOn ? L.T(" · ÉMISSION") : "");
    }

    // ---- PTT, driven by the transmission (TxTick) ----

    bool _pttKeyed;

    // what the PTT should be now: on from TxDelay before a step that needs
    // it until the end of the last one that does
    void DrivePtt()
    {
        var r = _cfg.Current.Radio;
        if (r.Ptt == "VOX" || _beacon is null) return;
        var now = DateTime.UtcNow;
        bool want = _beacon.Active && (_beacon.PttWanted(now) || _beacon.PttWanted(now.AddSeconds(Math.Clamp(r.TxDelaySeconds, 0, 2))));
        if (want == _pttKeyed) return;
        if (_rig is null)
        {
            if (want) { Log(L.T("PTT impossible : radio non connectée — émission arrêtée"), warning: true); _beacon.Stop(); StopCycle(); }
            _pttKeyed = false;
            return;
        }
        if (_rig.SetPtt(want)) _pttKeyed = want;
        else if (want) { _beacon.Stop(); StopCycle(); Log(L.T("émission arrêtée : PTT refusé"), warning: true); }
        else _pttKeyed = false;                         // release failed: reported, retried by nobody
    }

    void ReleasePtt()
    {
        if (_pttKeyed && _rig is not null) _rig.SetPtt(false);
        _pttKeyed = false;
    }

    // why the PTT cannot be used now, or null
    string? PttProblem()
    {
        var r = _cfg.Current.Radio;
        if (r.Ptt == "VOX") return null;
        if (r.Ptt == "CAT" && r.Cat == "none") return L.T("PTT par CAT sans CAT : choisir Hamlib dans Réglages › Radio");
        if (_rig is null) return L.T("radio non connectée (Réglages › Radio, journal)");
        if (r.Ptt == "CAT" && !_rig.Connected) return L.T("la radio ne répond pas en CAT");
        return null;
    }

    // ---- tests from the settings dialog ----

    // Closes the link, opens one with `r`, reads the frequency (and keys
    // the PTT 2 s), then reopens the link of the settings.
    public async Task<string> TestRadio(RadioSettings r, bool ptt)
    {
        if (_beacon?.Active == true) return L.T("émission en cours : test impossible");
        var old = _rig;
        _rig = null;
        _rigKey = "?";                                  // reopened after the test
        ++_rigGeneration;
        string result = await Task.Run(() =>
        {
            try { old?.Dispose(); } catch { }
            using var t = new Rig(r);
            var msgs = new List<string>();
            t.Message += (m, _) => { lock (msgs) msgs.Add(m); };
            try
            {
                t.Open();
                string f = t.FrequencyHz is double hz ? $"{hz / 1e6:0.000000} MHz" : L.T("liaison ouverte");
                if (!ptt) return "CAT : " + f;
                if (r.Ptt == "VOX") return L.T("PTT VOX : rien à commander (le transceiver émet sur l'audio)");
                if (!t.SetPtt(true)) return L.T("PTT refusé : ") + string.Join(" ; ", msgs);
                Thread.Sleep(2000);
                t.SetPtt(false);
                return L.F("PTT {0} : 2 s d'émission puis relâché", r.Ptt) + (msgs.Count > 0 ? " (" + string.Join(" ; ", msgs) + ")" : "");
            }
            catch (Exception e) { return L.T("échec : ") + e.Message; }
        });
        OpenLinks(force: true);
        return result;
    }

    // ---- fbbspots and MQTT: the same JSON (only what the frame carried) ----

    void PublishSpot(Spot s, double? rfMHz)
    {
        if (_mqtt is null && _fbb is null) return;
        if (SelectedSource is not { IsFile: false, Path: null }) return;         // not the replay of a file
        if (rfMHz is not double mhz) { if (!_mqttNoFreq) Log(L.T("fbbspots / MQTT : fréquence de la radio inconnue, spot non publié"), warning: true); _mqttNoFreq = true; return; }
        if (Callsign == "") return;
        _mqttNoFreq = false;
        var json = Mqtt.Json(s with { StartUtc = NearestSecond(s.StartUtc) }, Engine.Label(s.Proto), mhz * 1e6, Callsign, Locator);
        _fbb?.Publish(json);
        _mqtt?.Publish(json);
    }
    bool _mqttNoFreq;

    void CloseLinks()
    {
        ReleasePtt();
        _rigGeneration++;
        try { _rig?.Dispose(); } catch { }
        try { _mqtt?.Dispose(); } catch { }
        try { _fbb?.Dispose(); } catch { }
        _rig = null;
        _mqtt = null;
        _fbb = null;
    }

    // ---- Aide › Diagnostic fbbspots (Services/FbbspotsDiag) ----

    readonly System.Net.Http.HttpClient _diagHttp = new() { Timeout = TimeSpan.FromSeconds(15) };
    public System.Net.Http.HttpClient DiagHttp => _diagHttp;           // (the screenshots: a fake server)

    // with the saved settings, or (Réglages › Rapports, not saved yet) the
    // sending and key typed there; the steps also go to the journal (and
    // so to the testers' report)
    public async Task<List<DiagLine>> FbbspotsDiagnose(bool? enabled = null, string? key = null)
    {
        var m = _cfg.Reports;
        var ctx = new FbbspotsDiag.Context(enabled ?? m.Fbbspots, m.FbbspotsSite,
                                           key is { Length: > 0 } k ? k : Secret.Unprotect(m.FbbspotsKey),
                                           Callsign, _rx?.Live == true, RadioMHz, _fbb);
        var lines = await FbbspotsDiag.RunAsync(ctx, _diagHttp);
        Log(L.T("diagnostic fbbspots :"));
        foreach (var l in lines) Log("  " + l, warning: l.State == DiagState.Fail);
        return lines;
    }

    // the view opens the window (menu Aide)
    public event Action? FbbDiagRequested;
    [RelayCommand] void FbbDiag() => FbbDiagRequested?.Invoke();
}
