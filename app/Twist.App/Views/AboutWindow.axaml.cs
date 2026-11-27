using System.Reflection;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Twist.App.Services;
using Twist.App.ViewModels;
using Twist.App.Lang;

namespace Twist.App.Views;

// Name, versions (application, engine, Hamlib found) and where things are.
public partial class AboutWindow : Window
{
    public AboutWindow()
    {
        InitializeComponent();
        var v = Assembly.GetExecutingAssembly().GetName().Version;
        VersionText.Text = $"version {v?.Major}.{v?.Minor}.{v?.Build}";
        // the classic frame's name comes from the engine (core/twnames.h)
        var classic = Engine.Label(Twist.Native.TwlProto.Classic).Split(' ')[0];
        DescText.Text = L.F("Réception et émission des balises OOK {0} et TWIST (TWIST S, TWIST L), et des balises PI4, par la carte son, ", classic)
                      + L.T("avec radio par Hamlib et publication des spots en MQTT.");
        ClassicCredit.Text = L.F("{0} : mode de Jose Alberto Nieto Ros, EA5HVK ; trame classique compatible, sans aucun code du logiciel officiel.", classic);
        EngineText.Text = Engine.StatusText + " · " + (Hamlib.Tool("rigctld") is string r ? "Hamlib : " + r : L.T("Hamlib absent"));
        PathsText.Text = L.F("Réglages : {0}\nErreurs : {1}", Settings.DefaultPath, Program.ErrorLog);
    }

    // (open: the screenshots of the guide)
    public bool CreditsOpen { set => CreditsBox.IsExpanded = value; }

    void OnClose(object? sender, RoutedEventArgs e) => Close();
}
