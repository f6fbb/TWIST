using System.Diagnostics;
using Twist.Native;
using Twist.App.Lang;

namespace Twist.App.Services;

// One decoded frame, dated in UTC.
// DelayS: how much earlier than its arrival on the screen the frame was
// dated (sound card latency + receive chain delay; 0 for a file): the
// waterfall, which draws what arrives, puts the label that much later.
public sealed record Spot(DateTime StartUtc, TwlProto Proto, string Call, string Loc,
                          double Snr, double FreqHz, bool Deep, double DelayS = 0);

// The result of one decoding pass.
public sealed record PassInfo(DateTime EndUtc, double Seconds, int Spots, double AudioSeconds);

// The continuous receiver (docs/SPECIFICATION.md, section 2): the sound
// card (or a WAV file) feeds the capture ring buffer of libtwist; a
// dedicated thread, every 3 s (PeriodSeconds), copies the last 45 s and searches the
// frames that ENDED since the previous pass (twl_decode_recent); a
// frame seen by two passes is reported once.
//
// All the decoding runs in that single thread (FFTW is not thread-safe);
// the interface only receives events, and reads the level meter.
// Events are raised on the worker thread: the interface posts them to
// its own thread.
public sealed class Receiver : IDisposable, Controls.IAudioTap
{
    public const double Block = 45.0, Lookback = 4.0, DupSeconds = 5.0;

    // time between passes (1 to 15 s); Lookback covers it up to 4 s, a
    // longer period widens the window of each pass
    double _period = 3.0;
    public double PeriodSeconds
    {
        get => _period;
        set => _period = Math.Clamp(value, 1, 15);
    }

    // delay of the receive chain before the sound card (SDR, network,
    // virtual cable), s: start times are moved back by it
    public double ChainDelaySeconds { get; set; }

    public event Action<Spot>? SpotDecoded;
    public event Action<PassInfo>? PassDone;
    // A live pass that decoded something: its block (valid only during
    // the call, on the decoding thread), the rate, the UTC time of its
    // first sample and the new spots - to keep the WAV.
    public Action<ReadOnlyMemory<float>, int, DateTime, List<Spot>>? KeepPass { get; set; }
    // Keyed signals present at the end of each pass (frame in progress),
    // possibly none: the interface follows them from pass to pass.
    // (with the time of the newest audio of the pass: the clock of the
    // audio, which a fast replay runs much faster than the wall clock)
    public event Action<TwlKeyed[], DateTime>? KeyedSeen;
    // PI4 frames in progress (Services/Pi4Activity): their carriers, before KeyedSeen
    public event Action<List<double>, DateTime>? Pi4Seen;
    public const double KeyedWindow = 8.0, KeyedZ = 6.5;
    bool _live;
    public event Action<string>? Message;

    readonly TwistAudio _audio = new();
    readonly TwistDecoder _dec = new();
    readonly object _lock = new();
    TwlSettings _settings;
    string _known = "";
    Thread? _worker;
    volatile bool _run;
    readonly List<(TwlProto p, string call, DateTime start)> _seen = new();

    public bool Running => _run;

    // the replay of a file: its length, how far it is (seconds of audio),
    // finished to its end (not stopped); Fast can change while it plays
    public double FileSeconds { get; private set; }
    public double FilePosition => Volatile.Read(ref _filePos);
    double _filePos;
    public bool FileFinished { get; private set; }
    public bool Replaying => _run && !_live;
    public bool Fast { get => Volatile.Read(ref _fast); set => Volatile.Write(ref _fast, value); }
    bool _fast;
    public DateTime NextPassUtc { get; private set; }
    public string SourceName { get; private set; } = "";

    public Receiver()
    {
        LibTwist.SettingsDefault(out _settings);
    }

    // Settings for the next passes (any thread).
    public TwlSettings Settings
    {
        get { lock (_lock) return _settings; }
        set { lock (_lock) _settings = value; }
    }

    public string KnownCalls
    {
        get { lock (_lock) return _known; }
        set { lock (_lock) _known = value; }
    }

    public TwlAudioStats Stats => _audio.Stats;

    // ---- recordings (Services/Recorder.cs) ----

    // the audio kept by the capture: the passes read 45 s of it, the
    // recordings the last 2 minutes
    public const double KeepSeconds = 125;

    // the samples captured so far (the ring's count)
    long Count()
    {
        var st = _audio.Stats;
        return (long)Math.Round(st.Seconds * st.Rate);
    }

    // the newest `seconds` of audio (fewer if not captured yet), its rate
    // and the UTC time of its first sample (any thread); null if nothing
    public (float[] x, int fs, DateTime t0)? TakeLast(double seconds)
    {
        int fs = _audio.Stats.Rate;
        if (fs <= 0) return null;
        var buf = new float[(int)(Math.Min(seconds, KeepSeconds - 1) * fs)];
        int n = _audio.Last(buf);
        if (n <= 0) return null;
        var st = _audio.Stats;
        var end = DateTime.UtcNow.AddSeconds(-(_live ? st.LatencyS + ChainDelaySeconds : 0));
        return (n == buf.Length ? buf : buf[..n], fs, end.AddSeconds(-(double)n / fs));
    }

    // a continuous recording: fed from the decoding loop, from the moment
    // it starts, with no sample twice nor missed (live sources only)
    Recorder? _rec;
    long _recFrom = -1;
    readonly object _recLock = new();
    public bool Live => _run && _live;
    public bool Recording { get { lock (_recLock) return _rec is not null; } }
    // (returns the count it starts from: the tests)
    public long StartRecording(Recorder r)
    {
        lock (_recLock) { _rec = r; return _recFrom = Count(); }
    }
    public long CapturedSamples => Count();
    // the recording stopped: the caller closes its file
    public Recorder? StopRecording()
    {
        lock (_recLock) { var r = _rec; _rec = null; return r; }
    }

    // the samples since the last call: read when no block came in between
    // (the count the same before and after reading the ring), else at the
    // next turn of the loop, with those that came meanwhile
    void PullRecording()
    {
        lock (_recLock)
        {
            if (_rec is null) return;
            long now = Count();
            if (now < _recFrom) { _recFrom = 0; _rec.Gap(); }              // the capture restarted (woke up)
            long n = now - _recFrom;
            if (n <= 0) return;
            int fs = _audio.Stats.Rate;
            long cap = (long)((KeepSeconds - 2) * fs);
            if (n > cap) { _rec.Gap(); n = cap; }                           // fell behind the ring
            var buf = new float[n];
            int got = _audio.Last(buf);
            if (Count() != now || got != n) return;                         // a block came in: next turn
            _rec.Write(buf);
            _recFrom = now;
        }
    }

    // the waterfall reads the newest samples too (reading the ring
    // buffer from a second thread is safe: only the sound card writes)
    bool Controls.IAudioTap.Active => _run;
    int Controls.IAudioTap.Rate => _audio.Stats.Rate;
    int Controls.IAudioTap.Last(float[] buf) => _run ? _audio.Last(buf) : 0;

    // ---- sources ----

    // Input devices: (PortAudio index, display name). Called once, early:
    // PortAudio reads the device list at its first use.
    public static List<(int index, string name, bool isDefault)> InputDevices()
    {
        var list = new List<(int, string, bool)>();
        if (LibTwist.AudioAvailable() == 0) return list;
        var devs = TwistAudio.Devices();
        for (int i = 0; i < devs.Length; i++)
            if (devs[i].MaxIn > 0)
                list.Add((i, $"{devs[i].Name} ({devs[i].HostApi})", devs[i].IsDefaultIn != 0));
        return list;
    }

    // Output devices (transmission), same form.
    public static List<(int index, string name, bool isDefault)> OutputDevices()
    {
        var list = new List<(int, string, bool)>();
        if (LibTwist.AudioAvailable() == 0) return list;
        var devs = TwistAudio.Devices();
        for (int i = 0; i < devs.Length; i++)
            if (devs[i].MaxOut > 0)
                list.Add((i, $"{devs[i].Name} ({devs[i].HostApi})", devs[i].IsDefaultOut != 0));
        return list;
    }

    public void StartDevice(int device, string name)
    {
        Stop();
        _audio.StartInput(device, 0, KeepSeconds);
        _device = device;
        SourceName = name;
        _live = true;
        Say(L.F("capture : {0}, décodage à {1} Hz", name, _audio.Stats.Rate));
        StartWorker(() => LiveLoop());
    }

    // Replays a WAV file through the same chain, at real speed (fast:
    // as fast as the decoder goes - tests). UTC times count from start.
    public void StartFile(string path, bool fast = false)
    {
        Stop();
        var (x, fs) = Wav.ReadMono(path);
        FileSeconds = x.Length / (double)fs;
        Volatile.Write(ref _filePos, 0);
        FileFinished = false;
        Fast = fast;
        _audio.StartInput(LibTwist.AudioNone, fs, KeepSeconds);
        SourceName = Path.GetFileName(path);
        _live = false;
        Say(L.F("lecture : {0}, {1:0} s à {2} Hz", SourceName, x.Length / (double)fs, fs));
        StartWorker(() => FileLoop(x, fs, fast));
    }

    public void Stop()
    {
        // (a recording ends with its source: the view model closes its file)
        lock (_recLock) _rec = null;
        _run = false;
        if (_worker is not null && _worker != Thread.CurrentThread) _worker.Join();
        _worker = null;
        _audio.StopInput();
    }

    void StartWorker(Action loop)
    {
        _seen.Clear();
        _run = true;
        _worker = new Thread(() =>
        {
            try { loop(); }
            catch (Exception e) { Say(L.T("erreur du décodeur : ") + e.Message); Program.Log(e, "receiver"); }
            _run = false;
        }) { IsBackground = true, Name = "TWIST decoder", Priority = ThreadPriority.BelowNormal };
        _worker.Start();
    }

    void LiveLoop()
    {
        var buf = new float[(int)(Block * _audio.Stats.Rate) + 16];
        NextPassUtc = DateTime.UtcNow.AddSeconds(PeriodSeconds);
        var watch = new CaptureWatch(_audio.Stats.Seconds, DateTime.UtcNow);
        while (_run)
        {
            var before = DateTime.UtcNow;
            Thread.Sleep(100);
            var now = DateTime.UtcNow;
            try { PullRecording(); }
            catch (Exception e) { Say(L.T("enregistrement : ") + e.Message); StopRecording()?.Dispose(); }
            // the capture stopped (the computer went to sleep: the sound
            // card's stream gives nothing any more after it wakes up):
            // the device is opened again, every 5 s until audio comes
            switch (watch.Check(_audio.Stats.Seconds, now, (now - before).TotalSeconds))
            {
                case CaptureWatch.Verdict.Woke:
                case CaptureWatch.Verdict.Stalled:
                    Reopen(watch.Last == CaptureWatch.Verdict.Woke
                        ? L.F("réveil de l'ordinateur : capture {0} rouverte", SourceName)
                        : L.F("plus d'audio depuis {0:0} s : capture {1} rouverte", CaptureWatch.StallSeconds, SourceName));
                    watch = new CaptureWatch(_audio.Stats.Seconds, DateTime.UtcNow);
                    continue;
            }
            if (now < NextPassUtc) continue;
            NextPassUtc = NextPassUtc.AddSeconds(PeriodSeconds);
            if (NextPassUtc < now) NextPassUtc = now.AddSeconds(PeriodSeconds);   // fell behind: no burst
            var st = _audio.Stats;
            double delay = st.LatencyS + ChainDelaySeconds;
            RunPass(buf, DateTime.UtcNow.AddSeconds(-delay), delay);
        }
    }

    int _device;

    // closes and opens the capture device again (from the decoding thread)
    void Reopen(string what)
    {
        try
        {
            _audio.StopInput();
            _audio.StartInput(_device, 0, KeepSeconds);
            Say(what);
        }
        catch (Exception e) { Say(what + " — " + e.Message); }
    }

    void FileLoop(float[] x, int fs, bool fast)
    {
        var buf = new float[(int)(Block * fs) + 16];
        int step = (int)(PeriodSeconds * fs), pos = 0;
        var t0 = DateTime.UtcNow;
        // the file, then two periods of silence: a frame ending at the
        // very end of the file is still reached by a pass
        // real speed: pushed in 0.1 s pieces (as a sound card would, so that
        // the waterfall moves smoothly), a pass every 3 s of audio
        int piece = Math.Max(1, fs / 10);
        var clock = Stopwatch.StartNew();
        // (the clock restarts when Fast is switched off: real speed from there)
        long clockFrom = 0;
        bool wasFast = fast;
        while (_run && pos < x.Length + 2 * step)
        {
            for (int done = 0; done < step && _run; done += piece)
            {
                int m = Math.Min(piece, step - done);
                var p = new float[m];
                for (int i = 0; i < m; i++) p[i] = pos + done + i < x.Length ? x[pos + done + i] : 0f;
                _audio.Push(p, 1);
                PullRecording();                 // (the tests: a recording of a replay; the program records live sources only)
                Volatile.Write(ref _filePos, Math.Min(pos + done + m, x.Length) / (double)fs);
                bool f = Fast;
                if (wasFast && !f) { clock.Restart(); clockFrom = pos + done + m; }
                wasFast = f;
                if (!f)
                {
                    long due = (long)((pos + done + m - clockFrom) * 1000.0 / fs);
                    long left = due - clock.ElapsedMilliseconds;
                    if (left > 0) Thread.Sleep((int)left);
                }
            }
            pos += step;
            NextPassUtc = DateTime.UtcNow.AddSeconds(PeriodSeconds);
            RunPass(buf, t0.AddSeconds((double)pos / fs), 0);
        }
        if (_run) { FileFinished = true; Say(L.F("fin de {0}", SourceName)); }
        _run = false;
    }

    // One pass over the newest audio, the last sample being at endUtc
    // (delay: see Spot.DelayS).
    public void RunPass(float[] buf, DateTime endUtc, double delay = 0)
    {
        int n = _audio.Last(buf);
        int fs = _audio.Stats.Rate;
        TwlSettings s;
        string known;
        lock (_lock) { s = _settings; known = _known; }
        // shorter than the shortest frame searched (OOK 30.6 s, PI4 24.3 s)
        if (n < (s.Protocols.HasFlag(TwlProto.Pi4) ? 24.5 : 31) * fs) return;
        if (_knownApplied != known) { _dec.SetKnown(known); _knownApplied = known; }

        var samples = n == buf.Length ? buf : buf[..n];
        var sw = Stopwatch.StartNew();
        var spots = _dec.DecodeRecent(samples, fs, s, Math.Max(Lookback, PeriodSeconds + 1));
        sw.Stop();
        var t0 = endUtc.AddSeconds(-(double)n / fs);
        int fresh = 0;
        var news = new List<Spot>();
        foreach (var p in spots)
        {
            var start = t0.AddSeconds(p.Start);
            if (Seen(p.Proto, p.Call, start)) continue;
            fresh++;
            var spot = new Spot(start, p.Proto, p.Call, string.IsNullOrEmpty(p.Loc) ? "" : p.Loc,
                                p.Snr, p.Freq, p.Deep != 0, delay);
            news.Add(spot);
            SpotDecoded?.Invoke(spot);
        }
        if (Pi4Seen is { } ps && s.Protocols.HasFlag(TwlProto.Pi4))
            try { ps(Pi4Activity.Find(samples, n, fs, s.Pi4FCenter, s.Pi4Tol, KeyedWindow), endUtc); }
            catch (Exception e) { Program.Log(e, "pi4 activity"); }
        // (an OOK frame in progress only when an OOK protocol is searched)
        if (KeyedSeen is { } ks && (s.Protocols & TwlProto.All) != 0)
            try { ks(LibTwist.Keyed(samples, n, fs, s, KeyedWindow, KeyedZ), endUtc); }
            catch (Exception e) { Program.Log(e, "keyed"); }
        if (fresh > 0 && _live && KeepPass is { } keep)
            try { keep(samples.AsMemory(0, n), fs, t0, news); }
            catch (Exception e) { Say(L.T("WAV non enregistré : ") + e.Message); }
        PassDone?.Invoke(new PassInfo(endUtc, sw.Elapsed.TotalSeconds, fresh, (double)n / fs));
    }
    string? _knownApplied;

    bool Seen(TwlProto p, string call, DateTime start)
    {
        foreach (var s in _seen)
            if (s.p == p && s.call == call && Math.Abs((s.start - start).TotalSeconds) < DupSeconds) return true;
        if (_seen.Count >= 256) _seen.RemoveAt(0);
        _seen.Add((p, call, start));
        return false;
    }

    void Say(string m) => Message?.Invoke(m);

    public void Dispose()
    {
        Stop();
        _audio.Dispose();
        _dec.Dispose();
    }
}
