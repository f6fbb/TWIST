// TwistDemo - the C# bindings at work, the way the application will
// call the engine:
//   TwistDemo FILE.wav             decodes the whole file
//   TwistDemo FILE.wav -r 3        frames that ended in the last 3 s
//   TwistDemo -e CALL [LOCATOR]    chips of a TWIST S / L frame
//   TwistDemo -l                   audio devices
//   TwistDemo -t OUT.wav CALL LOCATOR
//                                  a beacon sequence as audio: TWIST L,
//                                  carrier 10 s, pause 5 s, classic frame

using System.Diagnostics;
using Twist.Native;

if (args.Length == 1 && args[0] == "-l")
{
    if (LibTwist.AudioAvailable() == 0) { Console.WriteLine("library built without live audio"); return 0; }
    var devs = TwistAudio.Devices();
    for (int i = 0; i < devs.Length; i++)
        Console.WriteLine($"{i,2} in {devs[i].MaxIn,2} out {devs[i].MaxOut,2} {devs[i].DefaultRate,6} Hz  {devs[i].HostApi,-16} {devs[i].Name}");
    return 0;
}
if (args.Length >= 2 && args[0] == "-e")
{
    var chips = new byte[LibTwist.MaxChips];
    var proto = args.Length > 2 ? TwlProto.TwistL : TwlProto.TwistS;
    int n = LibTwist.Encode(proto, args[1], args.Length > 2 ? args[2] : null, chips, chips.Length);
    if (n < 0) { Console.Error.WriteLine("cannot encode"); return 1; }
    Console.WriteLine($"{proto}, {n} chips, {n * LibTwist.ChipSeconds():F1} s");
    Console.WriteLine(string.Concat(chips.Take(n).Select(c => c == 1 ? '1' : '0')));
    return 0;
}
if (args.Length >= 4 && args[0] == "-t")
{
    LibTwist.TxSettingsDefault(out var ts);
    ts.Fs = 12000;
    using var tx = new TwistTransmitter();
    tx.Start(ts,
        TwlStep.Frame(TwlProto.TwistL, args[2], args[3]),
        TwlStep.Carrier(10),
        TwlStep.Pause(5),
        TwlStep.Frame(TwlProto.Classic, args[2]));
    var all = new List<float>();
    var buf = new float[4096];
    int m;
    while ((m = tx.Render(buf)) > 0) all.AddRange(buf.Take(m));
    Wav.WriteMono16(args[1], all.ToArray(), ts.Fs);
    Console.WriteLine($"{args[1]}: {all.Count / (double)ts.Fs:F3} s, {ts.ToneHz} Hz");
    return 0;
}
if (args.Length < 1)
{
    Console.Error.WriteLine("usage: TwistDemo FILE.wav [-r SECONDS] | -e CALL [LOCATOR]");
    return 2;
}

var (samples, fs) = Wav.ReadMono(args[0]);
double lookback = args.Length > 2 && args[1] == "-r" ? double.Parse(args[2], System.Globalization.CultureInfo.InvariantCulture) : 0;

using var dec = new TwistDecoder();
LibTwist.SettingsDefault(out var s);
Console.WriteLine($"TWIST Console {LibTwist.Version} (OPERA: {(LibTwist.HasClassic ? "yes" : "no")}, PI4: {(LibTwist.HasPi4 ? "yes" : "no")}), {samples.Length / (double)fs:F1} s at {fs} Hz");

var sw = Stopwatch.StartNew();
var spots = lookback > 0 ? dec.DecodeRecent(samples, fs, s, lookback) : dec.Decode(samples, fs, s);
sw.Stop();

foreach (var p in spots)
    Console.WriteLine($"{p.Start,6:F1} {p.End,6:F1} {p.Freq,7:F1} Hz {p.Snr,4:F0} dB  {LibTwist.ProtoName(p.Proto),-8} : {p.Call} {p.Loc}{(p.Deep != 0 ? " (deep)" : "")}");
Console.WriteLine($"{spots.Length} spot(s), {sw.Elapsed.TotalSeconds:F3} s");
return 0;

// Minimal PCM WAV reader (8/16/24/32-bit integer, channels averaged)
// and 16-bit mono writer.
static class Wav
{
    public static void WriteMono16(string path, float[] x, int fs)
    {
        using var w = new BinaryWriter(File.Create(path));
        w.Write("RIFF"u8); w.Write(36 + 2 * x.Length); w.Write("WAVEfmt "u8);
        w.Write(16); w.Write((short)1); w.Write((short)1); w.Write(fs); w.Write(2 * fs);
        w.Write((short)2); w.Write((short)16); w.Write("data"u8); w.Write(2 * x.Length);
        foreach (var v in x) w.Write((short)Math.Round(Math.Clamp(v, -1f, 1f) * 32767f));
    }

    public static (float[] samples, int fs) ReadMono(string path)
    {
        using var r = new BinaryReader(File.OpenRead(path));
        if (new string(r.ReadChars(4)) != "RIFF") throw new InvalidDataException("not a RIFF file");
        r.ReadUInt32();
        if (new string(r.ReadChars(4)) != "WAVE") throw new InvalidDataException("not a WAVE file");
        int ch = 0, fs = 0, bits = 0;
        while (true)
        {
            string id = new string(r.ReadChars(4));
            uint size = r.ReadUInt32();
            if (id == "fmt ")
            {
                r.ReadUInt16(); ch = r.ReadUInt16(); fs = (int)r.ReadUInt32();
                r.ReadUInt32(); r.ReadUInt16(); bits = r.ReadUInt16();
                r.BaseStream.Seek(size - 16 + (size & 1), SeekOrigin.Current);
            }
            else if (id == "data")
            {
                int bps = bits / 8, n = (int)(size / (uint)(bps * ch));
                var x = new float[n];
                byte[] raw = r.ReadBytes(n * bps * ch);
                for (int i = 0; i < n; i++)
                {
                    double acc = 0;
                    for (int c = 0; c < ch; c++)
                    {
                        int o = (i * ch + c) * bps;
                        acc += bits switch
                        {
                            8 => (raw[o] - 128) / 128.0,
                            16 => BitConverter.ToInt16(raw, o) / 32768.0,
                            24 => ((raw[o] | raw[o + 1] << 8 | raw[o + 2] << 16) << 8 >> 8) / 8388608.0,
                            _ => BitConverter.ToInt32(raw, o) / 2147483648.0,
                        };
                    }
                    x[i] = (float)(acc / ch);
                }
                return (x, fs);
            }
            else r.BaseStream.Seek(size + (size & 1), SeekOrigin.Current);
        }
    }
}
