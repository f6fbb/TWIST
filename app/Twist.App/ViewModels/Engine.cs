using Twist.Native;
using Twist.App.Lang;

namespace Twist.App.ViewModels;

// What the application knows of the native libraries (libtwcore and
// libtwist, required; libopera and libpi4, optional - chosen at
// installation). The application starts even without them (the
// interface can be shown), and says so in the status bar.
public static class Engine
{
    public static bool Loaded { get; }
    public static string Version { get; } = "";
    public static string Error { get; } = "";
    // the optional protocols, their library present
    public static bool HasClassic { get; }
    public static bool HasPi4 { get; }

    static Engine()
    {
        try
        {
            LibTwist.Check();
            Version = LibTwist.Version;
            Loaded = true;
            HasClassic = LibTwist.HasClassic;
            HasPi4 = LibTwist.HasPi4;
            Program.Log(null, "libtwcore, libtwist " + Version + " loaded" + (HasClassic ? ", libopera" : "") + (HasPi4 ? ", libpi4" : ""));
        }
        catch (Exception e)
        {
            Error = $"{e.GetType().Name}: {e.Message}";
            Program.Log(e, "libtwcore / libtwist");
        }
    }

    // The protocols that can be decoded and sent (their library present).
    public static TwlProto Available => !Loaded ? 0 : LibTwist.Available;
    public static bool Has(TwlProto p) => Loaded && LibTwist.Has(p);

    // Display labels come from the libraries (core/twnames.h): the source
    // code of the application does not spell the classic frame's name.
    public static string Label(TwlProto p) =>
        Loaded && LibTwist.ProtoName(p) is { Length: > 0 } n ? n : p switch
        {
            TwlProto.TwistS => "TWIST S",
            TwlProto.TwistL => "TWIST L",
            TwlProto.Pi4 => "PI4",
            _ => "classic",
        };

    // A label for a list of choices: an absent protocol says so.
    public static string Choice(TwlProto p) => Has(p) ? Label(p) : L.F("{0} (non installé)", Label(p));

    public static string StatusText => Loaded ? $"TWIST Console {Version}" : L.T("bibliothèques absentes : décodage impossible");
}
