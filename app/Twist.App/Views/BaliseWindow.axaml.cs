using Avalonia.Controls;
using Avalonia.Input;
using Twist.App.ViewModels;

namespace Twist.App.Views;

public partial class BaliseWindow : Window
{
    public BaliseWindow()
    {
        InitializeComponent();
        // Escape: immediate stop, as in the main window
        KeyDown += (_, e) =>
        {
            if (e.Key == Key.Escape && DataContext is BaliseViewModel b) { b.Main.StopTx(); e.Handled = true; }
        };
    }
}
