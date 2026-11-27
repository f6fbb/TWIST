using System.Diagnostics;
using System.Globalization;
using System.IO.Ports;
using System.Net.Sockets;
using System.Text;
using System.Text.RegularExpressions;
using Twist.App.Lang;

namespace Twist.App.Services;

// Hamlib's command-line tools: rigctld (the radio behind a TCP port) and
// rigctl (the model list). Looked for in a "hamlib" folder next to the
// program (the Windows package carries them), next to the program, then
// on the PATH (Linux: package libhamlib-utils).
public static class Hamlib
{
    public sealed record Model(int Number, string Maker, string Name)
    {
        public override string ToString() => $"{Maker} {Name} ({Number})";
    }

    public static string? Tool(string name)
    {
        string exe = OperatingSystem.IsWindows() ? name + ".exe" : name;
        var dir = AppContext.BaseDirectory;
        foreach (var p in new[] { Path.Combine(dir, "hamlib", exe), Path.Combine(dir, exe) })
            if (File.Exists(p)) return p;
        // (a Mac application started from the Finder has a short PATH:
        // the places of Homebrew and MacPorts too)
        var dirs = (Environment.GetEnvironmentVariable("PATH") ?? "").Split(Path.PathSeparator).ToList();
        if (OperatingSystem.IsMacOS()) dirs.AddRange(["/opt/homebrew/bin", "/usr/local/bin", "/opt/local/bin"]);
        foreach (var d in dirs)
            if (d != "" && File.Exists(Path.Combine(d, exe))) return Path.Combine(d, exe);
        return null;
    }

    static List<Model>? _models;

    // "rigctl -l": every model Hamlib knows, read once
    public static IReadOnlyList<Model> Models()
    {
        if (_models is not null) return _models;
        var list = new List<Model>();
        try
        {
            if (Tool("rigctl") is string rigctl)
            {
                var psi = new ProcessStartInfo(rigctl, "-l")
                { RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false, CreateNoWindow = true };
                using var p = Process.Start(psi)!;
                string all = p.StandardOutput.ReadToEnd();
                p.WaitForExit(5000);
                list = ParseModels(all);
            }
        }
        catch (Exception e) { Program.Log(e, "rigctl -l"); }
        return _models = list;
    }

    //  "  1035  Yaesu                  FT-991                  20230328.15     Stable      RIG_MODEL_FT991"
    static readonly Regex Line = new(@"^\s*(\d+)\s+(.+?)\s{2,}(.+?)\s{2,}\S+\s+\S+\s+RIG_MODEL_\S+\s*$");

    public static List<Model> ParseModels(string text)
    {
        var list = new List<Model>();
        foreach (var l in text.Split('\n'))
        {
            var m = Line.Match(l.TrimEnd('\r'));
            if (m.Success) list.Add(new Model(int.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture), m.Groups[2].Value.Trim(), m.Groups[3].Value.Trim()));
        }
        return list;
    }
}

// The radio, through rigctld (docs/SPECIFICATION.md, section 3):
//   - CAT "hamlib": TWIST starts rigctld for the model, port and speed
//     of the settings, on a local TCP port, and stops it at the end;
//   - CAT "net": a rigctld already running (here or on another computer);
//   - PTT by CAT: "T 1" (or "T 3", data / rear input, falling back to
//     "T 1" if the radio refuses it), "T 0";
//   - PTT by DTR or RTS: on the CAT port, rigctld's (-P DTR / RTS, same
//     "T" commands); on another port, opened here directly;
//   - the dial frequency read every second ("f"), for the RF frequency
//     of the spots.
// Commands are serialized on one connection with short timeouts: a
// radio that does not answer never blocks for more than a second.
public sealed class Rig : IDisposable
{
    readonly RadioSettings _s;
    readonly object _lock = new();
    Process? _daemon;
    TcpClient? _tcp;
    StreamReader? _rd;
    StreamWriter? _wr;
    SerialPort? _serial;
    Thread? _poll;
    volatile bool _run;
    readonly StringBuilder _daemonErr = new();
    bool _dataPttFailed;

    public event Action<string, bool>? Message;    // (text, warning), from any thread
    public bool Connected { get; private set; }
    public double? FrequencyHz { get; private set; }
    public bool PttOn { get; private set; }
    public string Description { get; }

    public const int LocalPort = 4534;             // rigctld started by TWIST (4532 left to others)

    public Rig(RadioSettings s)
    {
        _s = s;
        Description = s.Cat switch
        {
            "hamlib" => $"{(s.Rig == "" ? L.T("modèle ") + s.RigModel : s.Rig)}, {s.Port} {s.Baud} bauds",
            "net" => $"rigctld {s.NetHost}:{s.NetPort}",
            _ => L.T("pas de CAT"),
        };
    }

    bool CatPtt => _s.Ptt == "CAT" || ((_s.Ptt == "DTR" || _s.Ptt == "RTS") && _s.Cat == "hamlib" && SamePort);
    bool SamePort => string.Equals(_s.PttPort.Trim(), _s.Port.Trim(), StringComparison.OrdinalIgnoreCase) || _s.PttPort.Trim() == "";
    bool SerialPtt => (_s.Ptt == "DTR" || _s.Ptt == "RTS") && !CatPtt;

    // rigctld's arguments for the settings
    public static string DaemonArgs(RadioSettings s, int tcpPort)
    {
        var a = new List<string> { "-m", s.RigModel.ToString(CultureInfo.InvariantCulture) };
        if (s.Port.Trim() != "") { a.Add("-r"); a.Add(s.Port.Trim()); }
        if (s.Baud > 0) { a.Add("-s"); a.Add(s.Baud.ToString(CultureInfo.InvariantCulture)); }
        a.Add("-t"); a.Add(tcpPort.ToString(CultureInfo.InvariantCulture));
        a.Add("-T"); a.Add("127.0.0.1");
        bool same = string.Equals(s.PttPort.Trim(), s.Port.Trim(), StringComparison.OrdinalIgnoreCase) || s.PttPort.Trim() == "";
        if (s.Ptt == "CAT") { a.Add("-P"); a.Add("RIG"); }
        else if ((s.Ptt == "DTR" || s.Ptt == "RTS") && same) { a.Add("-P"); a.Add(s.Ptt); }
        return string.Join(' ', a.Select(x => x.Contains(' ') ? "\"" + x + "\"" : x));
    }

    // Starts rigctld if needed, connects, opens the PTT port. Throws
    // with the reason.
    public void Open()
    {
        if (_s.Cat == "hamlib")
        {
            if (_s.RigModel <= 0) throw new InvalidOperationException(L.T("modèle de radio Hamlib à choisir (Réglages › Radio)"));
            var exe = Hamlib.Tool("rigctld") ?? throw new InvalidOperationException(L.T("rigctld introuvable (Hamlib)"));
            var psi = new ProcessStartInfo(exe, DaemonArgs(_s, LocalPort))
            { UseShellExecute = false, CreateNoWindow = true, RedirectStandardError = true, RedirectStandardOutput = true };
            _daemon = Process.Start(psi) ?? throw new InvalidOperationException(L.T("rigctld ne démarre pas"));
            _daemon.ErrorDataReceived += (_, e) => { if (e.Data is string d) lock (_daemonErr) { _daemonErr.AppendLine(d); if (_daemonErr.Length > 4000) _daemonErr.Remove(0, 2000); } };
            _daemon.OutputDataReceived += (_, _) => { };
            _daemon.BeginErrorReadLine();
            _daemon.BeginOutputReadLine();
            Connect("127.0.0.1", LocalPort, TimeSpan.FromSeconds(8));
        }
        else if (_s.Cat == "net")
            Connect(_s.NetHost.Trim(), _s.NetPort, TimeSpan.FromSeconds(3));

        if (SerialPtt)
        {
            if (_s.PttPort.Trim() == "") throw new InvalidOperationException(L.F("port série du PTT {0} à choisir", _s.Ptt));
            _serial = new SerialPort(_s.PttPort.Trim()) { DtrEnable = false, RtsEnable = false };
            _serial.Open();
        }
        if (_tcp is not null)
        {
            Poll();                                     // a first answer, or the reason
            if (!Connected) throw new InvalidOperationException(L.T("la radio ne répond pas") + DaemonTail());
            _run = true;
            _poll = new Thread(PollLoop) { IsBackground = true, Name = "TWIST rig" };
            _poll.Start();
        }
    }

    string DaemonTail()
    {
        if (_daemon is null) return "";
        string t;
        lock (_daemonErr) t = _daemonErr.ToString().Trim();
        if (_daemon.HasExited) t = L.F("rigctld arrêté (code {0}) {1}", _daemon.ExitCode, t);
        var last = t.Split('\n').Where(x => x.Trim() != "").TakeLast(2);
        return t == "" ? "" : " : " + string.Join(" ; ", last).Trim();
    }

    void Connect(string host, int port, TimeSpan within)
    {
        var until = DateTime.UtcNow + within;
        Exception? last = null;
        while (DateTime.UtcNow < until)
        {
            if (_daemon is { HasExited: true }) break;
            try
            {
                var c = new TcpClient { NoDelay = true, ReceiveTimeout = 1500, SendTimeout = 1500 };
                if (!c.ConnectAsync(host, port).Wait(1000)) { c.Dispose(); throw new TimeoutException(L.T("délai dépassé")); }
                _tcp = c;
                var ns = c.GetStream();
                _rd = new StreamReader(ns, Encoding.ASCII);
                _wr = new StreamWriter(ns, Encoding.ASCII) { NewLine = "\n", AutoFlush = true };
                return;
            }
            catch (Exception e) { last = e.InnerException ?? e; Thread.Sleep(200); }
        }
        throw new InvalidOperationException(L.F("connexion à rigctld {0}:{1} impossible ({2}){3}", host, port, last?.Message ?? "?", DaemonTail()));
    }

    // one command, its reply lines up to "RPRT n" (set) or the value (get)
    string[] Command(string cmd, bool isSet)
    {
        lock (_lock)
        {
            if (_wr is null || _rd is null) throw new InvalidOperationException(L.T("pas de liaison CAT"));
            _wr.WriteLine(cmd);
            var lines = new List<string>();
            while (true)
            {
                var l = _rd.ReadLine() ?? throw new IOException(L.T("rigctld a fermé la liaison"));
                if (l.StartsWith("RPRT ")) { lines.Add(l); break; }
                lines.Add(l);
                if (!isSet) break;                     // a get answers its value (or RPRT on error)
            }
            return lines.ToArray();
        }
    }

    static int Rprt(string[] lines) =>
        lines.Length > 0 && lines[^1].StartsWith("RPRT ") && int.TryParse(lines[^1][5..], out var n) ? n : 0;

    void Poll()
    {
        try
        {
            var r = Command("f", false);
            if (r.Length > 0 && double.TryParse(r[0].Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out var hz) && hz > 0)
            {
                FrequencyHz = hz;
                if (!Connected) Message?.Invoke($"CAT : {Description}, {hz / 1e6:0.000000} MHz", false);
                Connected = true;
            }
            else throw new IOException(L.T("réponse « ") + string.Join(" ", r) + " »");
        }
        catch (Exception e)
        {
            if (Connected) Message?.Invoke(L.T("CAT perdu : ") + e.Message + DaemonTail(), true);
            Connected = false;
            FrequencyHz = null;
        }
    }

    void PollLoop()
    {
        while (_run)
        {
            Thread.Sleep(1000);
            if (!_run) break;
            if (!PttOn) Poll();                        // nothing else on the line while transmitting
            if (!Connected && _run && _s.Cat == "net")
            {
                try { CloseTcp(); Connect(_s.NetHost.Trim(), _s.NetPort, TimeSpan.FromSeconds(2)); Poll(); }
                catch { Thread.Sleep(4000); }
            }
        }
    }

    // Keys or releases the PTT; false with the reason in Message.
    public bool SetPtt(bool on)
    {
        try
        {
            if (_s.Ptt == "VOX") { PttOn = on; return true; }
            if (SerialPtt)
            {
                if (_serial is null) throw new InvalidOperationException(L.T("port PTT fermé"));
                if (_s.Ptt == "DTR") _serial.DtrEnable = on; else _serial.RtsEnable = on;
                PttOn = on;
                return true;
            }
            if (!on)
            {
                var r = Command("T 0", true);
                if (Rprt(r) != 0) throw new IOException(L.F("T 0 refusé (RPRT {0})", Rprt(r)));
                PttOn = false;
                return true;
            }
            // data / rear input PTT when asked and the radio knows it
            if (_s.Ptt == "CAT" && _s.DataPtt && !_dataPttFailed)
            {
                var r3 = Command("T 3", true);
                if (Rprt(r3) == 0) { PttOn = true; return true; }
                _dataPttFailed = true;
                Message?.Invoke(L.T("PTT données (T 3) refusé par la radio : PTT simple (T 1)"), false);
            }
            var r1 = Command("T 1", true);
            if (Rprt(r1) != 0) throw new IOException(L.F("T 1 refusé (RPRT {0})", Rprt(r1)));
            PttOn = true;
            return true;
        }
        catch (Exception e)
        {
            Message?.Invoke(L.F("PTT {0} impossible : {1}{2}", on ? "ON" : "OFF", e.Message, DaemonTail()), true);
            if (!on) PttOn = false;
            return false;
        }
    }

    void CloseTcp()
    {
        lock (_lock)
        {
            try { _tcp?.Dispose(); } catch { }
            _tcp = null; _rd = null; _wr = null;
        }
    }

    public void Dispose()
    {
        if (PttOn) SetPtt(false);                      // never leave the transmitter keyed
        _run = false;
        _poll?.Join(2000);
        try { _wr?.WriteLine("q"); } catch { }
        CloseTcp();
        try { _serial?.Close(); } catch { }
        if (_daemon is { HasExited: false })
            try { _daemon.Kill(); _daemon.WaitForExit(2000); } catch { }
        _daemon?.Dispose();
        Connected = false;
    }
}
