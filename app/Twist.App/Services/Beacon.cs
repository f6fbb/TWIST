using Twist.Native;
using Twist.App.Lang;

namespace Twist.App.Services;

// What goes to the sound card for one transmission.
public sealed record TxOutput(int Device, int Rate, double ToneHz, double Level, double MaxTxSeconds);

// A view of the transmission in progress, read by the interface.
public sealed record TxState(bool Active, bool Waiting, double Elapsed, double Total, int Step,
                             TwlStepKind Kind, int Chip, int NChips, bool Ptt, double PttSeconds);

// The transmitter (docs/SPECIFICATION.md, section 3, audio method):
// a sequence of steps rendered by libtwist (twl_tx_*) and played by the
// output sound card (twl_audio_start_output), which clocks the chips.
// Starting at a given second (beacon cycle): the stream opens a second
// ahead and plays silence until the first sample reaches the converter
// at that second, counted by libtwist from the stream's own timing
// (twl_audio_start_output_at) - the output latency, from tens of
// milliseconds to two seconds depending on the driver, is absorbed.
//
// The interface polls it (Poll, ten times a second): it keeps the
// watchdog - the time the PTT has been wanted without a break - and
// stops the transmission beyond the maximum. The PTT itself is VOX for
// now (the transceiver keys on the audio); CAT, DTR and RTS come with
// step 6 and will follow TxState.Ptt.
public sealed class Beacon : IDisposable
{
    readonly TwistAudio _audio = new();
    readonly TwistTransmitter _tx = new();
    bool _active, _stopping;
    double _maxTx, _pttOn;
    DateTime? _at;
    DateTime _lastPoll, _stopAt, _startCall;
    TwlStep[] _steps = [];

    public event Action<string>? Message;

    public bool Active => _active;
    public string Title { get; private set; } = "";
    public bool IsTune { get; private set; }
    // chips of each frame step of the sequence
    public byte[]?[] Chips { get; private set; } = [];
    public TxOutput? Output { get; private set; }
    // when the first sample reached the converter (timed starts: the
    // second asked, give or take the driver's report)
    public DateTime? StartedUtc => _active ? _audio.OutputStartedUtc : null;
    public double OutputLatency => _audio.OutputLatency;

    // ---- sequences ----

    // (a frame: its chips or PI4 symbols; CW: its text at its speed; libtwist)
    public static double StepSeconds(TwlStep s) =>
        LibTwist.StepSeconds(s) is double d && d >= 0 ? d : s.Kind == TwlStepKind.Frame ? LibTwist.FrameChips(s.Proto) * LibTwist.ChipSeconds() : s.Seconds;

    public static double TotalSeconds(IEnumerable<TwlStep> steps) => steps.Sum(StepSeconds);

    // the longest time the PTT stays on without a break (frames, carriers,
    // pauses that hold it): what the watchdog will measure
    public static double LongestPtt(IEnumerable<TwlStep> steps)
    {
        double run = 0, max = 0;
        foreach (var s in steps)
        {
            if (s.Kind == TwlStepKind.Pause && s.PttHold == 0) run = 0;
            else max = Math.Max(max, run += StepSeconds(s));
        }
        return max;
    }

    // Next start of a cycle "every periodMin minutes at second startSec",
    // counted from 00:00 UTC, strictly after `after`.
    public static DateTime NextStart(DateTime afterUtc, int periodMin, int startSec)
    {
        periodMin = Math.Clamp(periodMin, 1, 1440);
        startSec = Math.Clamp(startSec, 0, 59);
        var day = afterUtc.Date;
        var t = day.AddSeconds(startSec);
        long k = (long)Math.Floor((afterUtc - t).TotalMinutes / periodMin);
        t = t.AddMinutes(k * periodMin);
        while (t <= afterUtc) t = t.AddMinutes(periodMin);
        return t;
    }

    // Output device by its displayed name ("" = the system's default):
    // its index and its own sample rate.
    public static (int index, int rate) FindOutput(string name)
    {
        var devs = TwistAudio.Devices();
        for (int i = 0; i < devs.Length; i++)
        {
            var d = devs[i];
            if (d.MaxOut <= 0) continue;
            if (name == "" ? d.IsDefaultOut != 0 : $"{d.Name} ({d.HostApi})" == name)
                return (i, (int)Math.Round(d.DefaultRate));
        }
        throw new InvalidOperationException(name == "" ? L.T("aucune sortie audio") : L.F("sortie « {0} » absente", name));
    }

    // Starts a sequence now, or so that its first step begins at
    // `atUtc`. Throws with the reason if it cannot (already sending,
    // longer than the watchdog, engine or sound card refusal).
    public void Start(TwlStep[] steps, TxOutput o, string title, bool tune = false, DateTime? atUtc = null)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_active) throw new InvalidOperationException(L.T("émission déjà en cours"));
        if (steps.Length == 0) throw new ArgumentException(L.T("séquence vide"));
        double longest = LongestPtt(steps);
        if (longest > o.MaxTxSeconds)
            throw new ArgumentException(L.F("PTT maintenu {0:0} s sans interruption, plus que le chien de garde ({1:0} s)", longest, o.MaxTxSeconds));

        LibTwist.TxSettingsDefault(out var s);
        s.Fs = o.Rate;
        s.ToneHz = o.ToneHz;
        s.Level = Math.Clamp(o.Level, 0.001, 1.0);
        _tx.Start(s, steps);
        _at = atUtc is DateTime at && at > DateTime.UtcNow.AddSeconds(0.02) ? at : null;
        if (_at is DateTime t) _audio.StartOutputAt(_tx, t, o.Device);
        else _audio.StartOutput(_tx, o.Device);

        _steps = steps;
        _startCall = DateTime.UtcNow;
        Title = title;
        IsTune = tune;
        Output = o;
        Chips = steps.Select(x => x.Kind == TwlStepKind.Frame && x.Proto != TwlProto.Pi4 ? Encode(x) : null).ToArray();
        _maxTx = o.MaxTxSeconds;
        _pttOn = 0;
        _lastPoll = DateTime.UtcNow;
        _stopping = false;
        _active = true;
    }

    static byte[]? Encode(TwlStep s)
    {
        var c = new byte[LibTwist.MaxChips];
        int n = LibTwist.Encode(s.Proto, s.Call, s.Proto == TwlProto.TwistL ? s.Loc : null, c, c.Length);
        return n > 0 ? c[..n] : null;
    }

    // Whether the PTT is needed at time t by the sequence (frames,
    // carriers, pauses that hold it), counted from the moment its first
    // sample reached the converter (or the time asked, while waiting):
    // the caller keys it ahead by its TX delay.
    public bool PttWanted(DateTime t)
    {
        if (!_active || _stopping) return false;
        var origin = _audio.OutputStartedUtc ?? _at ?? _startCall;
        double x = (t - origin).TotalSeconds, acc = 0;
        if (x < 0) return false;
        foreach (var s in _steps)
        {
            double d = StepSeconds(s);
            if (x < acc + d) return s.Kind != TwlStepKind.Pause || s.PttHold != 0;
            acc += d;
        }
        return x < acc + 0.02;                         // the last 5 ms ramp
    }

    public DateTime? StartAt => _at;
    public TwlStep[] Steps => _steps;

    // Level while sending (Tune: adjusting the drive), 0..1.
    public void SetLevel(double level)
    {
        if (_active) _tx.SetLevel(Math.Clamp(level, 0.001, 1.0));
    }

    // Immediate stop: 5 ms fade-out, then the stream ends.
    public void Stop()
    {
        if (!_active || _stopping) return;
        _tx.Stop();
        _stopping = true;
        _stopAt = DateTime.UtcNow;
    }

    public TxState Poll()
    {
        var now = DateTime.UtcNow;
        if (!_active || _disposed) return new TxState(false, false, 0, 0, -1, 0, -1, 0, false, 0);
        var st = _tx.Status;
        bool playing = _audio.OutputActive;
        // finished: the sequence is done and the sound card has played it
        // out (or a stop did not end the stream within a second: forced)
        if (!playing || (_stopping && (now - _stopAt).TotalSeconds > 1))
        {
            _audio.StopOutput();
            _active = false;
            return new TxState(false, false, 0, 0, -1, 0, -1, 0, false, 0);
        }
        double dt = Math.Clamp((now - _lastPoll).TotalSeconds, 0, 1);
        _lastPoll = now;
        bool ptt = st.Ptt != 0 && st.Done == 0 && !(_at is not null && _audio.OutputStartedUtc is null);
        _pttOn = ptt ? _pttOn + dt : 0;
        if (_pttOn > _maxTx && !_stopping)
        {
            Stop();
            Message?.Invoke(L.F("chien de garde : émission arrêtée après {0:0} s de PTT continu", _maxTx));
        }
        // timed start: silence until the converter reaches the second
        bool waiting = _at is DateTime at && _audio.OutputStartedUtc is null;
        if (waiting)
            return new TxState(true, true, (now - _at!.Value).TotalSeconds, st.Total, -1, 0, -1, 0, false, 0);
        return new TxState(true, false, st.Elapsed, st.Total, st.Step,
                           (TwlStepKind)st.Kind, st.Chip, st.NChips, ptt, _pttOn);
    }

    bool _disposed;
    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _active = false;
        try { _tx.Stop(); Thread.Sleep(20); _audio.StopOutput(); } catch { }
        _audio.Dispose();
        _tx.Dispose();
    }
}
