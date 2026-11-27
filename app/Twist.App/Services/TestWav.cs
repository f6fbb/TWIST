using Twist.Native;

namespace Twist.App.Services;

// A test recording (Outils › WAV de test, docs/SPECIFICATION.md section 3,
// "Outils annexes"): one frame as the transmitter sends it (libtwist
// twl_tx: OOK with 5 ms ramps, or PI4 4-FSK), in white Gaussian noise at
// a given SNR, with some audio before and after - to check a receiver,
// TWIST Console itself, or to play into a transmitter.
//
// SNR: what the list of spots will show for it - dB in 2500 Hz, the power
// of the signal averaged over the frame (docs/RECEIVER.md: an OOK frame,
// on about half of its chips, is then 3 dB below its carrier; PI4, always
// on, is its carrier). White noise of standard deviation sigma at fs has
// sigma^2 * 2500 / (fs/2) in 2500 Hz; a tone A sin() has A^2 / 2, times
// the share of chips on for an OOK frame. The noise is set at sigma = 0.1
// and the tone from the SNR; everything is scaled down if the peaks
// would come near full scale (high SNR).
public static class TestWav
{
    public sealed record Request(TwlProto Proto, string Call, string Loc, double FreqHz, double? SnrDb,
                                 double BeforeS, double AfterS, int Rate, int Seed = 0);

    public const double NoiseSigma = 0.1;

    // the frame step of a request (PI4: the callsign, or "/" + locator when
    // the callsign is empty), or an error message
    public static TwlStep Step(Request r) =>
        r.Proto == TwlProto.Pi4 ? TwlStep.Pi4(r.Call, r.Call == "" ? r.Loc : "", r.FreqHz)
                                : TwlStep.Frame(r.Proto, r.Call, r.Proto == TwlProto.TwistL ? r.Loc : "");

    public static double FrameSeconds(Request r) => LibTwist.StepSeconds(Step(r));

    // the share of the frame the carrier is on (1 for PI4)
    public static double Duty(Request r)
    {
        if (r.Proto == TwlProto.Pi4) return 1;
        var c = new byte[LibTwist.MaxChips];
        int n = LibTwist.Encode(r.Proto, r.Call, r.Proto == TwlProto.TwistL ? r.Loc : null, c, c.Length);
        return n > 0 ? Math.Max(0.05, c.Take(n).Count(b => b != 0) / (double)n) : 0.5;
    }

    // the samples (mono, -1 to 1)
    public static float[] Make(Request r)
    {
        if (FrameSeconds(r) < 0) throw new ArgumentException(r.Proto == TwlProto.Pi4
            ? "message PI4 impossible : 8 caractères au plus (A-Z 0-9 /)" : "indicatif ou locator impossible à coder");
        var steps = new List<TwlStep>();
        if (r.BeforeS >= 0.05) steps.Add(TwlStep.Pause(r.BeforeS));
        steps.Add(Step(r));
        if (r.AfterS >= 0.05) steps.Add(TwlStep.Pause(r.AfterS));

        LibTwist.TxSettingsDefault(out var s);
        s.Fs = r.Rate;
        s.ToneHz = r.FreqHz;
        s.Level = 1.0;
        using var tx = new TwistTransmitter();
        tx.Start(s, steps.ToArray());
        var x = new List<float>(r.Rate * 60);
        var buf = new float[4096];
        int m;
        while ((m = tx.Render(buf)) > 0)
        {
            for (int i = 0; i < m; i++) x.Add(buf[i]);
            if (m < buf.Length) break;
        }
        var y = x.ToArray();

        double amp = 0.5, sigma = 0;
        if (r.SnrDb is double snr)
        {
            sigma = NoiseSigma;
            amp = Math.Sqrt(2 * sigma * sigma * 2500.0 / (r.Rate / 2.0) * Math.Pow(10, snr / 10) / Duty(r));
        }
        double scale = Math.Min(1.0, 0.9 / (amp + 4.5 * sigma));
        var rnd = r.Seed != 0 ? new Random(r.Seed) : new Random();
        for (int i = 0; i < y.Length; i++)
        {
            double n = 0;
            if (sigma > 0)
            {
                double u1 = 1.0 - rnd.NextDouble(), u2 = rnd.NextDouble();
                n = sigma * Math.Sqrt(-2 * Math.Log(u1)) * Math.Cos(2 * Math.PI * u2);
            }
            y[i] = (float)Math.Clamp((amp * y[i] + n) * scale, -1.0, 1.0);
        }
        return y;
    }

    // what the file says it holds (its comment, the tool's report)
    public static string Describe(Request r) =>
        $"{ViewModels.Engine.Label(r.Proto)} " + (r.Proto == TwlProto.Pi4 && r.Call == "" ? "/" + r.Loc : r.Call)
        + (r.Proto == TwlProto.TwistL ? " " + r.Loc : "")
        + $" · {r.FreqHz:0} Hz · " + (r.SnrDb is double d ? $"{d:0.#} dB" : "sans bruit")
        + $" · début {r.BeforeS:0.##} s · {r.Rate} Hz";

    // decodes it back as the receiver would (its protocol, its frequency at
    // the centre of the zone): what was found
    public static TwlSpot[] Check(float[] y, Request r)
    {
        LibTwist.SettingsDefault(out var s);
        if (r.Proto == TwlProto.Pi4) { s.Protocols = TwlProto.Pi4; s.Pi4FCenter = r.FreqHz; }
        else { s.Protocols = r.Proto; s.FCenter = r.FreqHz; }
        using var d = new TwistDecoder();
        return d.Decode(y, r.Rate, s);
    }
}
