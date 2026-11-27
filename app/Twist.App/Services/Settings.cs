using System.Text.Json;
using System.Text.Json.Serialization;
using Twist.App.Lang;

namespace Twist.App.Services;

// The settings of the application (docs/SPECIFICATION.md, section 5),
// saved as readable JSON in the user's profile:
//   Windows  %APPDATA%\TWIST\twist.json
//   Linux    ~/.config/TWIST/twist.json
// (TWIST_CONFIG=<file> uses another file: tests, several setups).
//
// What depends on the radio (audio, radio, decoding, beacon) lives in a
// profile; the file holds a list of profiles and the name of the current
// one, so that a 2 m and a 70 cm setup can be added later without
// changing the format. Unknown fields are ignored and missing ones take
// their default value: an older or newer file still loads.
public sealed class Settings
{
    public int Format { get; set; } = 1;
    public StationSettings Station { get; set; } = new();
    public string Profile { get; set; } = "Station";
    public List<ProfileSettings> Profiles { get; set; } = new() { new() };
    public ReportSettings Reports { get; set; } = new();
    public InterfaceSettings Interface { get; set; } = new();

    // the current profile (created if the name is unknown)
    [JsonIgnore]
    public ProfileSettings Current
    {
        get
        {
            var p = Profiles.FirstOrDefault(x => x.Name == Profile) ?? Profiles.FirstOrDefault();
            if (p is null) { p = new ProfileSettings { Name = Profile }; Profiles.Add(p); }
            Profile = p.Name;
            return p;
        }
    }

    public static string DefaultPath { get; } =
        Environment.GetEnvironmentVariable("TWIST_CONFIG") is { Length: > 0 } f ? f :
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "TWIST", "twist.json");

    static readonly JsonSerializerOptions Json = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping,   // é stays é
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true,
    };

    // Reads the file; defaults if it does not exist. A damaged file is
    // kept aside (twist.json.bad) and the defaults used: the program
    // must start whatever is in it. `problem` says what happened.
    public static Settings Load(string path, out string? problem)
    {
        problem = null;
        if (!File.Exists(path)) return new Settings();
        try
        {
            var s = JsonSerializer.Deserialize<Settings>(File.ReadAllText(path), Json) ?? new Settings();
            s.Normalize();
            return s;
        }
        catch (Exception e)
        {
            problem = L.F("réglages illisibles ({0}), valeurs par défaut ; fichier gardé en {1}.bad", e.Message, Path.GetFileName(path));
            try { File.Copy(path, path + ".bad", overwrite: true); } catch { }
            return new Settings();
        }
    }

    // Writes through a temporary file: a crash during the write never
    // leaves a half-written file.
    public void Save(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        var tmp = path + ".tmp";
        File.WriteAllText(tmp, JsonSerializer.Serialize(this, Json));
        File.Move(tmp, path, overwrite: true);
    }

    public Settings Clone() => JsonSerializer.Deserialize<Settings>(JsonSerializer.Serialize(this, Json), Json)!;

    // a deep copy of a part (handed to a background link)
    public static T CloneOf<T>(T o) => JsonSerializer.Deserialize<T>(JsonSerializer.Serialize(o, Json), Json)!;

    // nulls from a hand-edited file become defaults
    void Normalize()
    {
        Station ??= new();
        Reports ??= new();
        Interface ??= new();
        Profiles ??= new();
        Profiles.RemoveAll(p => p is null);
        foreach (var p in Profiles)
        {
            p.Audio ??= new(); p.Radio ??= new(); p.Decoding ??= new(); p.Beacon ??= new();
            p.Beacon.Steps ??= new();
            p.Beacon.Steps.RemoveAll(x => x is null);
            p.Name ??= "Station";
        }
        if (Profiles.Count == 0) Profiles.Add(new());
        Interface.Window ??= new();
        Interface.Waterfall ??= new();
        Interface.WaterfallWindow ??= new();
        Interface.SpotsWindow ??= new();
        Interface.JournalWindow ??= new();
        foreach (var w in new[] { Interface.WaterfallWindow, Interface.SpotsWindow, Interface.JournalWindow }) w.Window ??= new();
    }
}

public sealed class StationSettings
{
    public string Callsign { get; set; } = "";
    public string Locator { get; set; } = "";
    public string KnownCalls { get; set; } = "";          // comma-separated, deep search; "F5ZZO JN12AX": with its locator
    public bool LocatorLookup { get; set; } = true;       // locator of a callsign received without one: fbbspots
    public string LocatorApi { get; set; } = LocatorBook.DefaultApi;
}

public sealed class ProfileSettings
{
    public string Name { get; set; } = "Station";
    public AudioSettings Audio { get; set; } = new();
    public RadioSettings Radio { get; set; } = new();
    public DecodingSettings Decoding { get; set; } = new();
    public BeaconSettings Beacon { get; set; } = new();
}

public sealed class AudioSettings
{
    public string Input { get; set; } = "";               // device name as listed; "" = default device
    public string Output { get; set; } = "";
    public double TxLevel { get; set; } = 50;             // %
    public double ToneHz { get; set; } = 1500;
}

public sealed class RadioSettings
{
    // CAT through Hamlib's rigctld: "none"; "hamlib" = rigctld started by
    // TWIST for RigModel on Port at Baud; "net" = a rigctld already
    // running (this computer or another one) at NetHost:NetPort
    public string Cat { get; set; } = "none";
    public int RigModel { get; set; }                     // Hamlib model number (rigctl -l)
    public string Rig { get; set; } = "";                 // its name, for display
    public string Port { get; set; } = "";
    public int Baud { get; set; } = 38400;
    public string NetHost { get; set; } = "127.0.0.1";
    public int NetPort { get; set; } = 4532;
    public string Ptt { get; set; } = "VOX";              // VOX, CAT, DTR, RTS
    public string PttPort { get; set; } = "";             // DTR / RTS: the serial port (may be the CAT one)
    public bool DataPtt { get; set; } = true;             // CAT PTT on the data / rear (USB) audio input
    public double TxDelaySeconds { get; set; } = 0.2;     // PTT keyed this much before the audio
    public bool FreqFromCat { get; set; }
    public double FrequencyMHz { get; set; } = 144.489;   // dial frequency, USB
}

public sealed class DecodingSettings
{
    public bool Classic { get; set; } = true;
    public bool TwistS { get; set; } = true;
    public bool TwistL { get; set; } = true;
    // PI4 (4-FSK, OZ2M): off by default, in its own zone (its tones span
    // about 700 Hz): the "carrier" (OZ2M convention) +/- the width
    public bool Pi4 { get; set; }
    public double Pi4CenterHz { get; set; } = 800;
    public double Pi4WidthHz { get; set; } = 150;
    public double CenterHz { get; set; } = 1500;
    public double WidthHz { get; set; } = 200;            // ± around the centre
    public double SpreadHz { get; set; }                  // 0: clean carrier
    public double PassSeconds { get; set; } = 3;
    // delay of the receive chain outside the sound card (SDR, network,
    // virtual cable): start times are moved back by it
    public double ChainDelaySeconds { get; set; }
}

public sealed class BeaconSettings
{
    public int MaxTxSeconds { get; set; } = 120;           // watchdog
    public int TuneSeconds { get; set; } = 30;
    public int PeriodMinutes { get; set; } = 15;          // cycle: every N minutes (from 00:00 UTC)
    public int StartSecond { get; set; }                  // at this second
    public bool ReceiveBetween { get; set; } = true;
    public List<StepSettings> Steps { get; set; } = new() { new() };
    // PI4: the nominal frequency (OZ2M: tone 0 + 117.19 Hz), where the PI4
    // frames are sent, and the CW and carrier steps "at the PI4 frequency"
    public double Pi4ToneHz { get; set; } = 800;
    public double CwWpm { get; set; } = 12;               // CW identification
}

// One step of the beacon sequence. Kind: frame, carrier, pause, cw;
// Proto (frame): twistl, twists, classic, pi4; Seconds (carrier, pause);
// PttHold (pause): the PTT stays on. Pi4Message (PI4 frame): call,
// loc ("/" + locator) or alt (one, then the other, from one cycle to the
// next); AtPi4 (carrier, cw): at the PI4 nominal frequency, not at the
// transmission tone. CW sends the callsign and the locator.
public sealed class StepSettings
{
    public string Kind { get; set; } = "frame";
    public string Proto { get; set; } = "twistl";
    public double Seconds { get; set; } = 10;
    public bool PttHold { get; set; } = true;
    public string Pi4Message { get; set; } = "alt";
    public bool AtPi4 { get; set; }
}

public sealed class ReportSettings
{
    public string LogFolder { get; set; } =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "TWIST");
    public bool CsvLog { get; set; } = true;
    public bool KeepWav { get; set; }                     // the WAV of the passes that decoded something
    public int WavDays { get; set; } = 30;                // kept this many days
    public int WavMaxMB { get; set; } = 2000;             // and at most this much, oldest removed first
    // the continuous recording (Réception › Enregistrer en continu) stops
    // after this many minutes (0: no limit), or when the disk of the
    // journal folder has less free space than this (GB)
    public int RecordMaxMinutes { get; set; } = 60;
    public double RecordMinFreeGB { get; set; } = 2;
    // fbbspots: the spots sent with the key of this station (Fbbspots.cs)
    public bool Fbbspots { get; set; }
    public string FbbspotsKey { get; set; } = "";          // protected as the MQTT password (Secret)
    public string FbbspotsSite { get; set; } = Services.Fbbspots.DefaultSite;
    // MQTT: to a broker of one's own, as spyfbbclient
    public bool Mqtt { get; set; }
    public string MqttHost { get; set; } = "";
    public int MqttPort { get; set; } = 1883;
    public bool MqttTls { get; set; }
    public string MqttUser { get; set; } = "";
    // the password, protected for this Windows user (DPAPI, "dpapi:..."),
    // or as typed elsewhere (Secret.cs)
    public string MqttPassword { get; set; } = "";
    // fbbspots/<host>/spot, <host> = this computer (spyspots also listens to
    // spyfbbclient/<host>/spot, the topic of the earlier versions)
    public string MqttTopic { get; set; } = $"fbbspots/{Environment.MachineName}/spot";
}

public sealed class InterfaceSettings
{
    public string Language { get; set; } = "fr";
    public string Theme { get; set; } = "dark";
    public bool WaterfallOpen { get; set; } = true;
    public bool JournalOpen { get; set; } = true;
    public bool ShowTech { get; set; }                     // Affichage › Informations techniques
    public double WaterfallShare { get; set; } = 0.5;      // of the height of waterfall + spots
    public bool SpotSound { get; set; } = true;            // a chime on each spot received live
    // new versions: looked for at start and once a day (Services/Updates)
    public bool CheckUpdates { get; set; } = true;
    public string UpdateUrl { get; set; } = Updates.DefaultUrl;
    public WindowSettings Window { get; set; } = new();
    public WaterfallSettings Waterfall { get; set; } = new();
    // the parts that can be detached in a window of their own
    public PanelWindowSettings WaterfallWindow { get; set; } = new() { Window = new() { Width = 1100, Height = 480 } };
    public PanelWindowSettings SpotsWindow { get; set; } = new() { Window = new() { Width = 960, Height = 520 } };
    public PanelWindowSettings JournalWindow { get; set; } = new() { Window = new() { Width = 900, Height = 300 } };
}

public sealed class PanelWindowSettings
{
    public bool Detached { get; set; }
    public WindowSettings Window { get; set; } = new();
}

public sealed class WindowSettings
{
    public int X { get; set; } = int.MinValue;             // MinValue: let the system place it
    public int Y { get; set; } = int.MinValue;
    public double Width { get; set; } = 1440;
    public double Height { get; set; } = 900;
    public bool Maximized { get; set; }
}

public sealed class WaterfallSettings
{
    public double Gain { get; set; } = 50;
    public double Contrast { get; set; } = 40;
    public double LineSeconds { get; set; } = 0.2;
    // the band shown: "zones" (around the search zones), "200-2000" or
    // "300-2700"; empty: from FullBand (written by 0.23 and before, and
    // still written for them: true for 300-2700)
    public string Band { get; set; } = "";
    public bool FullBand { get; set; }
    public int MinuteLines { get; set; } = 1;             // a dashed line every N minutes (0: none)
    // a frame in progress labelled with its audio frequency instead of
    // "réception en cours"
    public bool LiveFrequency { get; set; }
}
