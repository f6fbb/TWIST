using System.Runtime.InteropServices;
using Avalonia;
using Twist.App.Lang;

namespace Twist.App;

internal static class Program
{
    // Any error that would end the program is written to
    // %LOCALAPPDATA%\TWIST\twist-error.log and shown in a message box:
    // a graphical program has no console, it would otherwise just vanish.
    public static string ErrorLog { get; } =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "TWIST", "twist-error.log");

    [STAThread]
    public static int Main(string[] args)
    {
        AppDomain.CurrentDomain.UnhandledException += (_, e) => Fatal(e.ExceptionObject as Exception);
        TaskScheduler.UnobservedTaskException += (_, e) => { Log(e.Exception, "unobserved task"); e.SetObserved(); };
        try
        {
            Log(null, $"start {Environment.ProcessPath} {string.Join(' ', args)} — {RuntimeInformation.OSDescription}");
            WaitFor(args);
            BuildAvaloniaApp(args.Contains("--software") || SoftwareByDefault()).StartWithClassicDesktopLifetime(args);
            return 0;
        }
        catch (Exception e)
        {
            Fatal(e);
            return 1;
        }
    }

    // --after <pid>: started by "Relancer TWIST Console" (language changed);
    // the program before closes first, its sound card and Hamlib port freed
    static void WaitFor(string[] args)
    {
        int i = Array.IndexOf(args, "--after");
        if (i < 0 || i + 1 >= args.Length || !int.TryParse(args[i + 1], out var pid)) return;
        try
        {
            using var p = System.Diagnostics.Process.GetProcessById(pid);
            p.WaitForExit(20_000);
            Thread.Sleep(500);
        }
        catch (ArgumentException) { }                // already gone
        catch (Exception e) { Log(e, "restart wait"); }
    }

    // also used by Twist.Screenshots. --software: CPU rendering only, for
    // machines whose graphics driver (OpenGL / ANGLE) does not work.
    public static AppBuilder BuildAvaloniaApp(bool software = false)
    {
        var b = AppBuilder.Configure<App>()
            .UsePlatformDetect()
            .WithInterFont()
            .LogToTrace();
        if (software)
            b = b.With(new Win32PlatformOptions { RenderingMode = [Win32RenderingMode.Software] })
                 .With(new X11PlatformOptions { RenderingMode = [X11RenderingMode.Software] })
                 .With(new AvaloniaNativePlatformOptions { RenderingMode = [AvaloniaNativeRenderingMode.Software] });
        return b;
    }

    // macOS before 11 (Catalina and older, often on Macs whose graphics
    // have no Metal, the OS installed with a patcher): CPU rendering,
    // unless TWIST_GPU=1; a .app is not started with arguments
    static bool SoftwareByDefault() =>
        OperatingSystem.IsMacOS() && !OperatingSystem.IsMacOSVersionAtLeast(11)
        && Environment.GetEnvironmentVariable("TWIST_GPU") != "1";

    public static void Log(Exception? e, string what = "error")
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(ErrorLog)!);
            File.AppendAllText(ErrorLog, $"{DateTime.Now:yyyy-MM-dd HH:mm:ss} {what}{(e is null ? "" : ":\n" + e)}\n");
        }
        catch { /* nowhere to report */ }
    }

    static void Fatal(Exception? e)
    {
        Log(e, "fatal");
        if (OperatingSystem.IsWindows())
            MessageBoxW(IntPtr.Zero,
                L.F("TWIST s'est arrêté sur une erreur :\n\n{0}: {1}\n\nDétails dans {2}", e?.GetType().Name, e?.Message, ErrorLog),
                "TWIST", 0x10);
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int MessageBoxW(IntPtr hWnd, string text, string caption, uint type);
}
