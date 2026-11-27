// LibTwist.cs - C# declarations of the libraries of TWIST Console
// (core/twltypes.h), and the layer the application uses over them:
//
//   libtwcore  (twcore.h)    audio, transmission, keying detector,
//                            arbitration - no protocol; required
//   libtwist   (libtwist.h)  TWIST S and TWIST L; required
//   libopera   (twcl.h)      the classic frame; optional
//   libpi4     (libpi4.h)    PI4; optional
//
// (.dll on Windows, .so on Linux, .dylib on macOS.) .NET finds them under
// these names next to the executable, or in the system library path.
// "libtwist", not "twist": on Windows twist.dll would clash with the
// application's own Twist.dll, Windows file names ignoring case.
//
// The optional libraries may be absent (not chosen at installation):
// LibTwist.HasClassic / HasPi4 say so, and the decoder, the encoders and
// the transmitter simply do without their protocol.
//
// The structures mirror the C ones field by field, in the same order
// (LayoutKind.Sequential, default alignment): any change on the C side
// must be made here too, and the API version bumped - LibTwist.Check()
// refuses a library whose API version differs.

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Twist.Native
{
    [Flags]
    public enum TwlProto
    {
        Classic = 1,    // classic frame (Op05): callsign, 239 chips (libopera)
        TwistS = 2,     // TWIST S: callsign, 239 chips (libtwist)
        TwistL = 4,     // TWIST L: callsign + locator, 321 chips (libtwist)
        All = 7,        // the three OOK frames
        Pi4 = 8,        // PI4 (4-FSK), in its own zone: Pi4FCenter +/- Pi4Tol (libpi4)
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct TwlSettings
    {
        public TwlProto Protocols;
        public double FCenter;      // Hz
        public double Tol;          // Hz, half-width of the search
        public double SpreadHz;     // > 0: rain scatter mode
        public int Deep;            // 1: deep search for known callsigns (classic frame)
        public int KnownOnly;
        public double EndMin;       // frame end window, s from block start
        public double EndMax;       // < 0: no limit
        public double Pi4FCenter;   // Hz, PI4 carrier (OZ2M convention), 800
        public double Pi4Tol;       // Hz, half-width of the PI4 search, 150
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct TwlSpot
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 12)] public string Call;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 8)] public string Loc;
        public TwlProto Proto;
        public int Deep;
        public double Start;        // s from the block start
        public double End;          // s
        public double Freq;         // Hz
        public double Snr;          // dB in 2500 Hz
        public double Drift;        // Hz/s
        public double Contrast;     // OOK: ON/OFF chip contrast (arbitration); PI4: 0
    }

    // A keyed signal present now (frame in progress), twl_keyed.
    [StructLayout(LayoutKind.Sequential)]
    public struct TwlKeyed
    {
        public double Freq;         // Hz
        public double Z;            // detector strength, robust sigmas
    }

    public enum TwlStepKind { Frame = 1, Carrier = 2, Pause = 3, Cw = 4 }

    // A step of a transmit sequence, as the application describes it: a
    // frame by its protocol, callsign and locator. The protocol's library
    // encodes it when the sequence starts (TwistTransmitter.Start,
    // LibTwist.StepSeconds).
    public struct TwlStep
    {
        public TwlStepKind Kind;
        public TwlProto Proto;      // Frame
        public string Call;         // Frame, Cw
        public string Loc;          // Frame (TWIST L; PI4 "/" + Loc when Call is ""), Cw
        public double Seconds;      // Carrier, Pause: 0.05 to 3600 s
        public int PttHold;         // Pause: 1 = PTT stays on
        public double ToneHz;       // this step's frequency (0: the sequence's; PI4: nominal)
        public double Wpm;          // Cw: words per minute, 5 to 40 (0: 12)

        public static TwlStep Frame(TwlProto p, string call, string loc = "")
            => new TwlStep { Kind = TwlStepKind.Frame, Proto = p, Call = call, Loc = loc };
        public static TwlStep Carrier(double s) => new TwlStep { Kind = TwlStepKind.Carrier, Seconds = s, Call = "", Loc = "" };
        public static TwlStep Pause(double s, bool holdPtt = false)
            => new TwlStep { Kind = TwlStepKind.Pause, Seconds = s, PttHold = holdPtt ? 1 : 0, Call = "", Loc = "" };
        // PI4: message = call, or "/" + loc when call is empty; toneHz = nominal frequency
        public static TwlStep Pi4(string call, string loc, double toneHz)
            => new TwlStep { Kind = TwlStepKind.Frame, Proto = TwlProto.Pi4, Call = call, Loc = loc, ToneHz = toneHz };
        // Morse: call, then loc if any
        public static TwlStep Cw(string call, string loc, double wpm, double toneHz = 0)
            => new TwlStep { Kind = TwlStepKind.Cw, Call = call, Loc = loc, Wpm = wpm, ToneHz = toneHz };
    }

    // twl_step_t: the step as the transmitter takes it, a frame by its symbols.
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct TwlNativeStep
    {
        public TwlStepKind Kind;
        public TwlProto Proto;
        public int NSym;
        public double SymS;
        public double SpacingHz;
        public double OffsetHz;
        public double Seconds;
        public int PttHold;
        public double ToneHz;
        public double Wpm;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string Text;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 324)] public byte[] Sym;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct TwlTxSettings
    {
        public int Fs;              // Hz (default 48000)
        public double ToneHz;       // default 1500
        public double Level;        // peak amplitude 0..1 (default 0.5)
        public double RampS;        // default 0.005
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct TwlTxStatus
    {
        public int Step, Kind, Chip, NChips;
        public double Elapsed, Total;
        public int Ptt, Done;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct TwlAudioDevice
    {
        // UTF-8 in the library (PortAudio gives UTF-8 names on every
        // system): kept as bytes and decoded here, not as ANSI strings,
        // else "Mixage stéréo" would read "Mixage stÃ©rÃ©o" on Windows.
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 128)] public byte[] NameUtf8;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)] public byte[] HostApiUtf8;
        public string Name => Utf8(NameUtf8);
        public string HostApi => Utf8(HostApiUtf8);
        public int MaxIn, MaxOut;
        public double DefaultRate;
        public int IsDefaultIn, IsDefaultOut;

        static string Utf8(byte[]? b)
        {
            if (b is null) return "";
            int n = Array.IndexOf(b, (byte)0);
            return System.Text.Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct TwlAudioStats
    {
        public double LevelDbfs, RmsDbfs, Seconds, LatencyS;
        public int Overruns, Rate;
    }

    // ---- the four libraries, as declared in C ----

    static class Core            // libtwcore (twcore.h)
    {
        const string Lib = "libtwcore";
        public const int ApiVersion = 1;    // TWL_CORE_API_VERSION
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int twl_api_version();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr twl_version();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern double twl_chip_seconds();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int twl_keyed(float[] samples, int n, int fs, ref TwlSettings s,
            double windowS, double zMin, [Out] TwlKeyed[] outp, int max);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int twl_arbitrate([In, Out] TwlSpot[] spots, int n);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void twl_tx_settings_default(out TwlTxSettings s);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr twl_tx_create();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void twl_tx_destroy(IntPtr t);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int twl_tx_start(IntPtr t, ref TwlTxSettings s, [In] TwlNativeStep[] steps, int nsteps);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int twl_tx_render(IntPtr t, [Out] float[] buf, int n);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void twl_tx_stop(IntPtr t);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int twl_tx_set_level(IntPtr t, double level);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void twl_tx_status(IntPtr t, out TwlTxStatus st);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr twl_tx_last_error(IntPtr t);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern double twl_tx_step_seconds(ref TwlNativeStep st);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int twl_audio_available();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int twl_audio_device_count();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int twl_audio_device_info(int index, out TwlAudioDevice d);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr twl_audio_create();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void twl_audio_destroy(IntPtr a);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int twl_audio_start_input(IntPtr a, int device, int rate, double keepS);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int twl_audio_last(IntPtr a, [Out] float[] buf, int n);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int twl_audio_push(IntPtr a, float[] x, int frames, int channels);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void twl_audio_input_stats(IntPtr a, out TwlAudioStats st);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void twl_audio_stop_input(IntPtr a);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int twl_audio_start_output(IntPtr a, int device, IntPtr tx);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int twl_audio_start_output_at(IntPtr a, int device, IntPtr tx, double utc);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern double twl_audio_output_started(IntPtr a);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern double twl_audio_output_latency(IntPtr a);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int twl_audio_output_active(IntPtr a);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void twl_audio_stop_output(IntPtr a);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr twl_audio_last_error(IntPtr a);
    }

    static class Tw              // libtwist (libtwist.h)
    {
        const string Lib = "libtwist";
        public const int ApiVersion = 1;    // TWT_API_VERSION
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int twt_api_version();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void twt_settings_default(out TwlSettings s);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr twt_create();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void twt_destroy(IntPtr d);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)] public static extern int twt_set_known(IntPtr d, string calls);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int twt_decode(IntPtr d, float[] samples, int n, int fs, ref TwlSettings s, [Out] TwlSpot[] spots, int maxSpots);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int twt_decode_recent(IntPtr d, float[] samples, int n, int fs, ref TwlSettings s, double lookback, [Out] TwlSpot[] spots, int maxSpots);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr twt_last_error(IntPtr d);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr twt_proto_name(TwlProto p);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int twt_encode(TwlProto proto, string call, string? loc, byte[] chips, int maxChips);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int twt_frame_chips(TwlProto proto);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int twt_frame_step(TwlProto proto, string call, string? loc, out TwlNativeStep st);
    }

    static class Cl              // libopera (twcl.h)
    {
        const string Lib = "libopera";
        public const int ApiVersion = 1;    // TWCL_API_VERSION
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int twcl_api_version();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr twcl_create();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void twcl_destroy(IntPtr d);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)] public static extern int twcl_set_known(IntPtr d, string calls);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int twcl_decode(IntPtr d, float[] samples, int n, int fs, ref TwlSettings s, [Out] TwlSpot[] spots, int maxSpots);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int twcl_decode_recent(IntPtr d, float[] samples, int n, int fs, ref TwlSettings s, double lookback, [Out] TwlSpot[] spots, int maxSpots);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr twcl_last_error(IntPtr d);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr twcl_proto_name(TwlProto p);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int twcl_encode(string call, byte[] chips, int maxChips);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int twcl_frame_step(string call, out TwlNativeStep st);
    }

    static class P4              // libpi4 (libpi4.h)
    {
        const string Lib = "libpi4";
        public const int ApiVersion = 1;    // PI4L_API_VERSION
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int pi4l_api_version();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr pi4l_create();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void pi4l_destroy(IntPtr d);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)] public static extern int pi4l_set_known(IntPtr d, string calls);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int pi4l_decode(IntPtr d, float[] samples, int n, int fs, ref TwlSettings s, [Out] TwlSpot[] spots, int maxSpots);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int pi4l_decode_recent(IntPtr d, float[] samples, int n, int fs, ref TwlSettings s, double lookback, [Out] TwlSpot[] spots, int maxSpots);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr pi4l_last_error(IntPtr d);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int pi4l_frame_step(string call, string loc, out TwlNativeStep st);
    }

    // ---- what the application uses ----

    public static class LibTwist
    {
        public const int MaxChips = 321;    // TWL_MAX_CHIPS
        public const int Pi4Symbols = 146;

        // The optional libraries: present and of the right API version
        // (checked once). A library absent - its protocol not chosen at
        // installation - is not an error.
        static readonly Lazy<bool> _classic = new(() => Probe(() => Cl.twcl_api_version() == Cl.ApiVersion));
        static readonly Lazy<bool> _pi4 = new(() => Probe(() => P4.pi4l_api_version() == P4.ApiVersion));
        public static bool HasClassic => _classic.Value;
        public static bool HasPi4 => _pi4.Value;

        static bool Probe(Func<bool> f)
        {
            try { return f(); }
            catch (DllNotFoundException) { return false; }
            catch (EntryPointNotFoundException) { return false; }
            catch (BadImageFormatException) { return false; }
        }

        // The protocols that can be decoded and sent here.
        public static TwlProto Available => TwlProto.TwistS | TwlProto.TwistL
            | (HasClassic ? TwlProto.Classic : 0) | (HasPi4 ? TwlProto.Pi4 : 0);

        public static bool Has(TwlProto p) => (Available & p) == p && p != 0;

        public static int GetApiVersion() => Core.twl_api_version();
        public static string Version => Marshal.PtrToStringAnsi(Core.twl_version()) ?? "";
        public static double ChipSeconds() => Core.twl_chip_seconds();

        public static void SettingsDefault(out TwlSettings s) => Tw.twt_settings_default(out s);

        // Keyed signals in the last windowS seconds of samples[0..n), in the
        // search zone of s, strongest first (decoding thread only: FFTW).
        public static TwlKeyed[] Keyed(float[] samples, int n, int fs, TwlSettings s, double windowS = 8, double zMin = 6)
        {
            var o = new TwlKeyed[16];
            int k = Core.twl_keyed(samples, n, fs, ref s, windowS, zMin, o, o.Length);
            return k > 0 ? o[..k] : Array.Empty<TwlKeyed>();
        }

        // Chips of an OOK frame (1 = carrier on); -1 if it cannot be
        // encoded here (or its library is absent).
        public static int Encode(TwlProto proto, string call, string? loc, byte[] chips, int maxChips) => proto switch
        {
            TwlProto.Classic => HasClassic ? Cl.twcl_encode(call, chips, maxChips) : -1,
            TwlProto.TwistS or TwlProto.TwistL => Tw.twt_encode(proto, call, loc, chips, maxChips),
            _ => -1,
        };

        public static int FrameChips(TwlProto proto) => proto == TwlProto.Classic ? 239 : Tw.twt_frame_chips(proto);

        // Display label: the classic frame's usual name + " 05" (from its
        // library, core/twnames.h), "TWIST S", "TWIST L", "PI4"; "" if the
        // library of that protocol is absent.
        public static string ProtoName(TwlProto p) => p switch
        {
            TwlProto.Classic => HasClassic ? Marshal.PtrToStringAnsi(Cl.twcl_proto_name(p)) ?? "" : "",
            TwlProto.Pi4 => "PI4",
            _ => Marshal.PtrToStringAnsi(Tw.twt_proto_name(p)) ?? "",
        };

        // ---- transmission ----
        public static void TxSettingsDefault(out TwlTxSettings s) => Core.twl_tx_settings_default(out s);

        // The step as the transmitter takes it: a frame encoded by its
        // protocol's library. null and the reason if it cannot be.
        public static TwlNativeStep? ToNative(TwlStep s, out string? error)
        {
            error = null;
            TwlNativeStep n;
            if (s.Kind == TwlStepKind.Frame)
            {
                int rc;
                string call = s.Call ?? "", loc = s.Loc ?? "";
                switch (s.Proto)
                {
                    case TwlProto.Classic:
                        if (!HasClassic) { error = "library of the classic frame not installed"; return null; }
                        rc = Cl.twcl_frame_step(call, out n);
                        break;
                    case TwlProto.Pi4:
                        if (!HasPi4) { error = "PI4 library not installed"; return null; }
                        rc = P4.pi4l_frame_step(call, loc, out n);
                        break;
                    case TwlProto.TwistS or TwlProto.TwistL:
                        rc = Tw.twt_frame_step(s.Proto, call, s.Proto == TwlProto.TwistL ? loc : null, out n);
                        break;
                    default:
                        error = "unknown protocol"; return null;
                }
                if (rc != 0)
                {
                    error = $"cannot encode {(call == "" ? "/" + loc : call + (loc != "" && s.Proto != TwlProto.TwistS ? " " + loc : ""))}";
                    return null;
                }
            }
            else
            {
                n = new TwlNativeStep
                {
                    Kind = s.Kind,
                    Text = s.Kind == TwlStepKind.Cw ? ((s.Call ?? "") + ((s.Loc ?? "") != "" ? " " + s.Loc : "")) : "",
                };
            }
            n.Sym ??= new byte[324];
            n.Text ??= "";
            n.Seconds = s.Seconds;
            n.PttHold = s.PttHold;
            n.ToneHz = s.ToneHz;
            n.Wpm = s.Wpm;
            return n;
        }

        // duration of a step (s), -1 if it cannot be sent
        public static double StepSeconds(TwlStep st)
        {
            if (ToNative(st, out _) is not TwlNativeStep n) return -1;
            return Core.twl_tx_step_seconds(ref n);
        }

        // ---- live audio ----
        public const int AudioDefault = -1, AudioNone = -2;
        public static int AudioAvailable() => Core.twl_audio_available();
        public static int AudioDeviceCount() => Core.twl_audio_device_count();
        public static int AudioDeviceInfo(int index, out TwlAudioDevice d) => Core.twl_audio_device_info(index, out d);

        // Throws if the required libraries are missing or do not match
        // these declarations.
        public static void Check()
        {
            int v = Core.twl_api_version();
            if (v != Core.ApiVersion)
                throw new InvalidOperationException($"libtwcore API version {v}, {Core.ApiVersion} expected");
            v = Tw.twt_api_version();
            if (v != Tw.ApiVersion)
                throw new InvalidOperationException($"libtwist API version {v}, {Tw.ApiVersion} expected");
        }
    }

    // The decoders of the libraries present, as one: each decodes its own
    // protocols among those asked, then their spots are arbitrated
    // (twl_arbitrate: the classic frame first, TWIST, PI4) and sorted by
    // start. Released by Dispose (using) or by the GC.
    public sealed class TwistDecoder : IDisposable
    {
        IntPtr tw, cl, p4;
        readonly TwlSpot[] buf = new TwlSpot[64];

        public TwistDecoder()
        {
            LibTwist.Check();
            tw = Tw.twt_create();
            if (tw == IntPtr.Zero) throw new OutOfMemoryException("twt_create");
            if (LibTwist.HasClassic) cl = Cl.twcl_create();
            if (LibTwist.HasPi4) p4 = P4.pi4l_create();
        }

        public int SetKnown(string calls)
        {
            int n = Check(Tw.twt_set_known(tw, calls), Tw.twt_last_error(tw));
            if (cl != IntPtr.Zero) n = Check(Cl.twcl_set_known(cl, calls), Cl.twcl_last_error(cl));
            if (p4 != IntPtr.Zero) Check(P4.pi4l_set_known(p4, calls), P4.pi4l_last_error(p4));
            return n;
        }

        public TwlSpot[] Decode(float[] samples, int fs, TwlSettings s) => Run(samples, fs, s, 0);

        // Frames that ended during the last `lookback` seconds of the block.
        public TwlSpot[] DecodeRecent(float[] samples, int fs, TwlSettings s, double lookback) => Run(samples, fs, s, lookback);

        TwlSpot[] Run(float[] x, int fs, TwlSettings s, double lookback)
        {
            if ((s.Protocols & (TwlProto.All | TwlProto.Pi4)) == 0)
                throw new InvalidOperationException("no protocol selected");
            var all = new List<TwlSpot>();
            void Add(int n) { for (int i = 0; i < n; i++) all.Add(buf[i]); }
            if (cl != IntPtr.Zero && (s.Protocols & TwlProto.Classic) != 0)
                Add(Check(lookback > 0 ? Cl.twcl_decode_recent(cl, x, x.Length, fs, ref s, lookback, buf, buf.Length)
                                       : Cl.twcl_decode(cl, x, x.Length, fs, ref s, buf, buf.Length), Cl.twcl_last_error(cl)));
            if ((s.Protocols & (TwlProto.TwistS | TwlProto.TwistL)) != 0)
                Add(Check(lookback > 0 ? Tw.twt_decode_recent(tw, x, x.Length, fs, ref s, lookback, buf, buf.Length)
                                       : Tw.twt_decode(tw, x, x.Length, fs, ref s, buf, buf.Length), Tw.twt_last_error(tw)));
            if (p4 != IntPtr.Zero && (s.Protocols & TwlProto.Pi4) != 0)
                Add(Check(lookback > 0 ? P4.pi4l_decode_recent(p4, x, x.Length, fs, ref s, lookback, buf, buf.Length)
                                       : P4.pi4l_decode(p4, x, x.Length, fs, ref s, buf, buf.Length), P4.pi4l_last_error(p4)));
            if (all.Count == 0) return Array.Empty<TwlSpot>();
            var arr = all.ToArray();
            int k = Core.twl_arbitrate(arr, arr.Length);
            var r = arr[..k];
            Array.Sort(r, (a, b) => a.Start.CompareTo(b.Start));
            return r;
        }

        static int Check(int rc, IntPtr err) =>
            rc >= 0 ? rc : throw new InvalidOperationException(Marshal.PtrToStringAnsi(err) ?? "");

        public void Dispose()
        {
            if (tw != IntPtr.Zero) { Tw.twt_destroy(tw); tw = IntPtr.Zero; }
            if (cl != IntPtr.Zero) { Cl.twcl_destroy(cl); cl = IntPtr.Zero; }
            if (p4 != IntPtr.Zero) { P4.pi4l_destroy(p4); p4 = IntPtr.Zero; }
            GC.SuppressFinalize(this);
        }

        ~TwistDecoder() => Dispose();
    }
}

namespace Twist.Native
{
    // A transmit sequence rendered to audio. Render() is called by the
    // sound card callback (or a loop writing a file); Stop() and Status
    // may be called from the user interface.
    public sealed class TwistTransmitter : IDisposable
    {
        IntPtr h;
        internal IntPtr Handle => h;

        public TwistTransmitter()
        {
            LibTwist.Check();
            h = Core.twl_tx_create();
            if (h == IntPtr.Zero) throw new OutOfMemoryException("twl_tx_create");
        }

        public void Start(TwlTxSettings s, params TwlStep[] steps)
        {
            var n = new TwlNativeStep[steps.Length];
            for (int i = 0; i < steps.Length; i++)
                n[i] = LibTwist.ToNative(steps[i], out var err) ?? throw new ArgumentException(err);
            if (Core.twl_tx_start(h, ref s, n, n.Length) != 0)
                throw new ArgumentException(Marshal.PtrToStringAnsi(Core.twl_tx_last_error(h)) ?? "");
        }

        // Fills buf; returns the samples that belong to the sequence (0 = finished).
        public int Render(float[] buf) => Core.twl_tx_render(h, buf, buf.Length);

        public void Stop() => Core.twl_tx_stop(h);

        // Level (0 < level <= 1) while playing, any thread: follows in ~10 ms, no click.
        public bool SetLevel(double level) => Core.twl_tx_set_level(h, level) == 0;

        public TwlTxStatus Status { get { Core.twl_tx_status(h, out var st); return st; } }

        public void Dispose()
        {
            if (h != IntPtr.Zero) { Core.twl_tx_destroy(h); h = IntPtr.Zero; }
            GC.SuppressFinalize(this);
        }

        ~TwistTransmitter() => Dispose();
    }
}

namespace Twist.Native
{
    // Live audio: capture into the ring buffer, playback of a sequence.
    public sealed class TwistAudio : IDisposable
    {
        IntPtr h;

        public TwistAudio()
        {
            LibTwist.Check();
            h = Core.twl_audio_create();
            if (h == IntPtr.Zero) throw new OutOfMemoryException("twl_audio_create");
        }

        public static TwlAudioDevice[] Devices()
        {
            int n = LibTwist.AudioDeviceCount();
            var list = new List<TwlAudioDevice>();
            for (int i = 0; i < n; i++)
                if (LibTwist.AudioDeviceInfo(i, out var d) == 0) list.Add(d);
            return list.ToArray();
        }

        // rate 0 = the device's default; keepS: seconds kept (60)
        public void StartInput(int device = LibTwist.AudioDefault, int rate = 0, double keepS = 60)
            => Check(Core.twl_audio_start_input(h, device, rate, keepS));

        // The newest samples (up to buf.Length), oldest first; returns their number.
        public int Last(float[] buf) => Core.twl_audio_last(h, buf, buf.Length);

        public int Push(float[] x, int channels) => Core.twl_audio_push(h, x, x.Length / channels, channels);

        public TwlAudioStats Stats { get { Core.twl_audio_input_stats(h, out var st); return st; } }

        public void StopInput() => Core.twl_audio_stop_input(h);

        // Plays a started sequence (prepared at the device's DefaultRate).
        public void StartOutput(TwistTransmitter tx, int device = LibTwist.AudioDefault)
            => Check(Core.twl_audio_start_output(h, device, tx.Handle));

        // Same, the first sample reaching the converter at `utc` (silence before).
        public void StartOutputAt(TwistTransmitter tx, DateTime utc, int device = LibTwist.AudioDefault)
            => Check(Core.twl_audio_start_output_at(h, device, tx.Handle, UnixSeconds(utc)));

        // When the first sample reached the converter (null while waiting).
        public DateTime? OutputStartedUtc
        {
            get
            {
                double t = Core.twl_audio_output_started(h);
                return t > 0 ? DateTime.UnixEpoch.AddTicks((long)(t * TimeSpan.TicksPerSecond)) : null;
            }
        }

        public double OutputLatency => Core.twl_audio_output_latency(h);

        static double UnixSeconds(DateTime utc) => (utc.ToUniversalTime() - DateTime.UnixEpoch).Ticks / (double)TimeSpan.TicksPerSecond;

        public bool OutputActive => Core.twl_audio_output_active(h) != 0;

        public void StopOutput() => Core.twl_audio_stop_output(h);

        void Check(int rc) { if (rc != 0) throw new InvalidOperationException(Marshal.PtrToStringAnsi(Core.twl_audio_last_error(h)) ?? ""); }

        public void Dispose()
        {
            if (h != IntPtr.Zero) { Core.twl_audio_destroy(h); h = IntPtr.Zero; }
            GC.SuppressFinalize(this);
        }

        ~TwistAudio() => Dispose();
    }
}
