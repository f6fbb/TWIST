using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Threading;
using Twist.App.ViewModels;
using Twist.App.Views;

namespace Twist.App;

public partial class App : Application
{
    public override void Initialize() => AvaloniaXamlLoader.Load(this);

    public override void OnFrameworkInitializationCompleted()
    {
        // errors in the interface: logged (Program.ErrorLog) before the program ends
        Dispatcher.UIThread.UnhandledException += (_, e) => Program.Log(e.Exception, "interface");
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            // the interface language, before any window (Lang/L.cs)
            Lang.L.Set(Services.Settings.Load(Services.Settings.DefaultPath, out _).Interface.Language);
            Lang.L.HookControls();
            desktop.MainWindow = new MainWindow { DataContext = new MainWindowViewModel() };
            Program.Log(null, "main window created");
        }
        base.OnFrameworkInitializationCompleted();
    }
}
