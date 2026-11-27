using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Avalonia.VisualTree;
using Twist.App.ViewModels;
using Twist.App.Lang;

namespace Twist.App.Views;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        Title = "TWIST Console  –  " + Services.Report.Version;   // "TWIST Console  –  0.19.0" (testers, 0.18)
        _waterfall = new("Waterfall", WaterfallPanel, v => v.WaterfallDetached, v => v.WaterfallDetached = false, i => i.WaterfallWindow);
        _spots = new("Spots", SpotsPanel, v => v.SpotsDetached, v => v.SpotsDetached = false, i => i.SpotsWindow);
        _journal = new("Journal", JournalPanel, v => v.JournalDetached, v => v.JournalDetached = false, i => i.JournalWindow);
        KeyDown += OnKey;
        // a right click selects the line under the mouse first (the menu
        // Supprimer la ligne acts on the selected line)
        SpotsGrid.AddHandler(PointerPressedEvent, (_, e) =>
        {
            if (!e.GetCurrentPoint(SpotsGrid).Properties.IsRightButtonPressed) return;
            if ((e.Source as Avalonia.Visual)?.FindAncestorOfType<DataGridRow>(includeSelf: true) is { DataContext: SpotViewModel s })
                SpotsGrid.SelectedItem = s;
        }, Avalonia.Interactivity.RoutingStrategies.Tunnel, handledEventsToo: true);
        DataContextChanged += (_, _) =>
        {
            if (Vm is null) return;
            Vm.PickWavFile = PickWav;
            RestorePlace(Vm.Config.Interface.Window);
            Vm.PropertyChanged += (_, e) =>
            {
                if (e.PropertyName is nameof(MainWindowViewModel.WaterfallDetached) or nameof(MainWindowViewModel.SpotsDetached)
                    or nameof(MainWindowViewModel.JournalDetached) or nameof(MainWindowViewModel.WaterfallOpen)
                    or nameof(MainWindowViewModel.JournalOpen))
                    PlaceParts();
            };
            // a new version: its changes; the installer started, the program closes
            Vm.UpdateDialogRequested += ShowUpdateWindow;
            Vm.FbbDiagRequested += () => ShowFbbDiag(this);
            Vm.QuitForUpdate += () => Avalonia.Threading.Dispatcher.UIThread.Post(Close);
            // the journal follows its newest line
            Vm.Journal.CollectionChanged += (_, _) =>
                Avalonia.Threading.Dispatcher.UIThread.Post(() => JournalScroll.ScrollToEnd(), Avalonia.Threading.DispatcherPriority.Background);
        };
        // the detached parts are shown once this window is
        Opened += (_, _) => PlaceParts();
        Closing += (_, e) =>
        {
            if (Vm is null || e.Cancel) return;
            _closing = true;                         // the detached parts stay detached for the next start
            SavePlace(Vm.Config.Interface.Window);
            foreach (var p in Parts()) if (p.Window is { } w) SavePlace(w, p.SettingsOf(Vm!).Window);
            Vm.SaveNow();
            // the windows of their own go with this one
            foreach (var p in Parts()) p.Window?.Close();
            _balise?.Close();
        };
        Closed += (_, _) => Vm?.Dispose();
    }

    async Task<string?> PickWav()
    {
        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = L.T("Rejouer un enregistrement"),
            AllowMultiple = false,
            FileTypeFilter = [new FilePickerFileType(L.T("Fichiers WAV")) { Patterns = ["*.wav", "*.WAV"] }],
        });
        return files.Count > 0 ? files[0].TryGetLocalPath() : null;
    }

    MainWindowViewModel? Vm => DataContext as MainWindowViewModel;

    // F2 / F3: waterfall and journal panels (brought to the front if detached)
    void OnKey(object? sender, KeyEventArgs e)
    {
        if (Vm is null) return;
        if (e.Key == Key.S && e.KeyModifiers == KeyModifiers.Control) { Vm.SaveLastCommand.Execute(null); e.Handled = true; }
        if (e.Key == Key.F2) { if (_waterfall.Window is { } w) w.Activate(); else Vm.WaterfallOpen = !Vm.WaterfallOpen; e.Handled = true; }
        if (e.Key == Key.F3) { if (_journal.Window is { } w) w.Activate(); else Vm.JournalOpen = !Vm.JournalOpen; e.Handled = true; }
        if (e.Key == Key.Escape && (Vm.TxActive || Vm.TxWaiting || Vm.CycleRunning)) { Vm.StopTx(); e.Handled = true; }
    }

    // one Balise window, brought to the front if already open
    BaliseWindow? _balise;
    void OnBalise(object? sender, RoutedEventArgs e)
    {
        if (Vm is null) return;
        if (_balise is { IsVisible: true }) { _balise.Activate(); return; }
        var bvm = new BaliseViewModel(Vm);
        _balise = new BaliseWindow { DataContext = bvm };
        _balise.Closed += (_, _) => { bvm.Detach(); _balise = null; };
        _balise.Show();                              // not owned: not minimized with this window
    }

    async void OnSettings(object? sender, RoutedEventArgs e)
    {
        if (Vm is null) return;
        var outputs = Services.Receiver.OutputDevices().Select(d => d.name).ToList();
        var dlg = new SettingsWindow { DataContext = new SettingsViewModel(Vm.Config, Vm.ConfigPath, Vm.InputNames, outputs) { TestRadio = Vm.TestRadio } };
        ((SettingsViewModel)dlg.DataContext).Diagnose = (on, key) => ShowFbbDiag(dlg, on, key);
        var lang = Vm.Config.Interface.Language;
        dlg.Apply = s => Vm.ApplyConfig(s);                 // Appliquer: the window stays open
        var saved = await dlg.ShowDialog<Services.Settings?>(this);
        if (saved is not null) Vm.ApplyConfig(saved);
        if (Vm.Config.Interface.Language != lang) await LanguageNotice(Vm.Config.Interface.Language);
    }

    // ---- the three parts: in this window, or each in a window of its own ----

    sealed class Part(string title, Border panel, Func<MainWindowViewModel, bool> detached,
                      Action<MainWindowViewModel> putBack, Func<Services.InterfaceSettings, Services.PanelWindowSettings> of)
    {
        public readonly string Title = title;
        public readonly Border Panel = panel;
        public bool Detached(MainWindowViewModel v) => detached(v);
        public void PutBack(MainWindowViewModel v) => putBack(v);
        public Services.PanelWindowSettings SettingsOf(MainWindowViewModel v) => of(v.Config.Interface);
        public Window? Window;
    }

    bool _closing;
    readonly Part _waterfall, _spots, _journal;
    IEnumerable<Part> Parts() => [_waterfall, _spots, _journal];
    // the windows of the detached parts (waterfall, spots, journal order)
    public IReadOnlyList<Window> DetachedWindows => Parts().Select(p => p.Window).OfType<Window>().ToList();

    // each part where the settings want it, then the main window's grid:
    // the parts left in it, top to bottom, one of them taking the height
    // left (the spots; else the open waterfall; else the open journal)
    void PlaceParts()
    {
        if (Vm is not { } vm || !IsVisible || _closing) return;
        foreach (var p in Parts())
        {
            if (p.Detached(vm) && p.Window is null) Detach(p);
            else if (!p.Detached(vm) && p.Window is { } w)
            {
                p.Window = null;
                SavePlace(w, p.SettingsOf(Vm!).Window);
                ((Border)w.Content!).Child = null;           // the panel comes back here
                w.UpdateLayout();
                w.Close();
            }
        }
        var here = Parts().Where(p => p.Window is null).ToList();
        // the waterfall and the spots both here, the waterfall open: they
        // share the height, at the place of the splitter between them
        bool share = here.Contains(_waterfall) && here.Contains(_spots) && vm.WaterfallOpen;
        var fill = here.Contains(_spots) ? _spots
                 : here.Contains(_waterfall) && vm.WaterfallOpen ? _waterfall
                 : here.Contains(_journal) && vm.JournalOpen ? _journal : null;
        PanelGrid.Children.Clear();
        PanelGrid.RowDefinitions.Clear();
        double wf = Math.Clamp(vm.Config.Interface.WaterfallShare, 0.15, 0.85);
        foreach (var p in here)
        {
            if (PanelGrid.RowDefinitions.Count > 0)
            {
                // 12 px between two parts; between the waterfall and the
                // spots sharing the height, these 12 px are the splitter
                PanelGrid.RowDefinitions.Add(new RowDefinition(new GridLength(12)));
                if (share && p == _spots)
                {
                    var sp = new GridSplitter
                    {
                        ResizeDirection = GridResizeDirection.Rows, Background = Avalonia.Media.Brushes.Transparent,
                        HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Stretch,
                    };
                    ToolTip.SetTip(sp, L.T("Tirer pour partager la hauteur entre le waterfall et les spots"));
                    sp.DragCompleted += (_, _) => KeepShare();
                    Grid.SetRow(sp, PanelGrid.RowDefinitions.Count - 1);
                    PanelGrid.Children.Add(sp);
                }
            }
            var panel = p.Panel;
            Grid.SetRow(panel, PanelGrid.RowDefinitions.Count);
            var height = share && p == _waterfall ? new GridLength(wf, GridUnitType.Star)
                       : share && p == _spots ? new GridLength(1 - wf, GridUnitType.Star)
                       : p == fill ? GridLength.Star : GridLength.Auto;
            // never squeezed to nothing: the waterfall keeps about 120 px of
            // picture, the spots their header and four lines
            PanelGrid.RowDefinitions.Add(new RowDefinition(height)
            {
                MinHeight = p == _spots ? 150 : p == _waterfall && vm.WaterfallOpen && height.IsStar ? 190 : 0,
            });
            PanelGrid.Children.Add(panel);
        }
        if (fill is null) PanelGrid.RowDefinitions.Add(new RowDefinition(GridLength.Star));
        // the waterfall and the journal: their height given by the grid when
        // they share it or fill it, else their own
        WaterfallBox.Height = share || fill == _waterfall || _waterfall.Window is not null ? double.NaN : 340;
        JournalScroll.Height = fill == _journal || _journal.Window is not null ? double.NaN : 96;
    }

    // the splitter moved: the share of the waterfall kept in the settings
    void KeepShare()
    {
        if (Vm is not { } vm) return;
        int w = Grid.GetRow(WaterfallPanel), s = Grid.GetRow(SpotsPanel);
        double hw = PanelGrid.RowDefinitions[w].ActualHeight, hs = PanelGrid.RowDefinitions[s].ActualHeight;
        if (hw + hs <= 0) return;
        vm.Config.Interface.WaterfallShare = Math.Round(hw / (hw + hs), 3);
        vm.SaveSoon();
    }

    void Detach(Part p)
    {
        var panel = p.Panel;
        PanelGrid.Children.Remove(panel);
        UpdateLayout();                                  // (no layout of this window left on the panel)
        var w = new Window
        {
            Title = "TWIST Console · " + L.T(p.Title),
            Icon = Icon,
            DataContext = DataContext,
            Content = new Border { Padding = new Avalonia.Thickness(12), Child = panel },
            MinWidth = 420, MinHeight = 160,
            WindowStartupLocation = WindowStartupLocation.CenterScreen,
        };
        RestorePlace(w, p.SettingsOf(Vm!).Window);
        w.KeyDown += OnKey;
        w.Closing += (_, _) =>
        {
            if (_closing || w.Content is not Border { Child: not null } b) return;
            SavePlace(w, p.SettingsOf(Vm!).Window);
            b.Child = null;                              // the panel, free to go back
            w.UpdateLayout();
        };
        w.Closed += (_, _) =>
        {
            if (_closing || p.Window != w) return;
            p.Window = null;
            p.PutBack(Vm!);                              // its window closed (×): back in the main window
        };
        p.Window = w;
        // not owned by this window: on Windows, owned windows are minimized
        // with it, and testers keep one part on screen alone (0.18); they
        // are closed with it (Closing, below)
        w.Show();
    }

    // ---- size and place of the windows, kept in the settings ----

    void RestorePlace(Services.WindowSettings w) => RestorePlace(this, w);

    static void RestorePlace(Window win, Services.WindowSettings w)
    {
        if (w.Width >= win.MinWidth && w.Height >= win.MinHeight) { win.Width = w.Width; win.Height = w.Height; }
        // only if that place is still on a screen (a second monitor may be gone)
        if (w.X != int.MinValue && win.Screens.All.Any(sc => sc.WorkingArea.Contains(new Avalonia.PixelPoint(w.X + 40, w.Y + 20))))
        {
            win.WindowStartupLocation = WindowStartupLocation.Manual;
            win.Position = new Avalonia.PixelPoint(w.X, w.Y);
        }
        if (w.Maximized) win.WindowState = WindowState.Maximized;
    }

    void SavePlace(Services.WindowSettings w) => SavePlace(this, w);

    static void SavePlace(Window win, Services.WindowSettings w)
    {
        w.Maximized = win.WindowState == WindowState.Maximized;
        if (win.WindowState == WindowState.Normal)
        {
            w.Width = Math.Round(win.Width); w.Height = Math.Round(win.Height);
            w.X = win.Position.X; w.Y = win.Position.Y;
        }
    }

    // the spots: a doubtful callsign greyed; Delete removes the line
    void OnSpotRow(object? sender, DataGridRowEventArgs e) =>
        e.Row.Classes.Set("doubt", e.Row.DataContext is SpotViewModel { Doubtful: true });

    void OnSpotsKey(object? sender, KeyEventArgs e)
    {
        if (e.Key != Key.Delete || Vm?.SelectedSpot is not { } s) return;
        Vm.DeleteSpotCommand.Execute(s);
        e.Handled = true;
    }

    void OnAbout(object? sender, RoutedEventArgs e) => new AboutWindow().ShowDialog(this);

    // the guides next to the program (installed with it)
    void OnGuide(object? sender, RoutedEventArgs e) => OpenBeside("TESTEURS.txt", L.T("guide des testeurs absent (installé avec le programme)"));
    void OnUserGuide(object? sender, RoutedEventArgs e) => OpenBeside("TWIST-Console-Guide-utilisateur.pdf", L.T("guide de l'utilisateur absent (installé avec le programme)"));

    void OpenBeside(string name, string missing)
    {
        var p = Path.Combine(AppContext.BaseDirectory, name);
        if (!File.Exists(p)) { Vm?.Log(missing, warning: true); return; }
        try { System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(p) { UseShellExecute = true }); }
        catch (Exception ex) { Vm?.Log(L.T("ouverture du guide impossible : ") + ex.Message, warning: true); }
    }

    void OnTools(object? sender, RoutedEventArgs e) =>
        new ToolsWindow { DataContext = new ToolsViewModel() }.Show(this);

    // (the same window, on its "WAV de test" tab)
    void OnToolsWav(object? sender, RoutedEventArgs e) =>
        new ToolsWindow { DataContext = new ToolsViewModel(), WavTab = true }.Show(this);

    void OnQuit(object? sender, RoutedEventArgs e) => Close();

    // the language is read once, at start: say so in both languages, the
    // one shown now first
    async Task LanguageNotice(string lang)
    {
        var fr = lang == "en" ? "L'interface sera en anglais au prochain démarrage du programme."      // no-tr
                              : "L'interface sera en français au prochain démarrage du programme.";    // no-tr
        var en = lang == "en" ? "The interface will be in English from the next start of the program."   // no-tr
                              : "The interface will be in French from the next start of the program.";   // no-tr
        var (first, second) = L.English ? (en, fr) : (fr, en);
        // restarted now if asked (testers, 0.18): the new program waits for
        // this one to be gone (sound card, rigctld port) before opening
        if (await ProfileWindow.Message(L.English ? "Language" : "Langue", first, second,       // no-tr
                                        L.T("Relancer TWIST Console"), L.T("Plus tard")).ShowDialog<bool>(this))
            Restart();
    }

    // the fbbspots diagnostic, over `owner` (this window, or the settings
    // with the sending and key typed there)
    void ShowFbbDiag(Window owner, bool? enabled = null, string? key = null)
    {
        if (Vm is not { } vm) return;
        new DiagWindow(() => vm.FbbspotsDiagnose(enabled, key)).Show(owner);
    }

    // a new version, or the answer to Aide › Rechercher une mise à jour
    // (Views/UpdateWindow): one window at a time
    UpdateWindow? _updateWindow;
    void ShowUpdateWindow(bool available, string? message)
    {
        if (Vm is null) return;
        _updateWindow?.Close();
        _updateWindow = new UpdateWindow(Vm, available, message);
        _updateWindow.Closed += (_, _) => _updateWindow = null;
        _updateWindow.Show(this);
    }

    void Restart()
    {
        try
        {
            var psi = new System.Diagnostics.ProcessStartInfo(Environment.ProcessPath!) { UseShellExecute = false };
            psi.ArgumentList.Add("--after");
            psi.ArgumentList.Add(Environment.ProcessId.ToString());
            foreach (var a in Environment.GetCommandLineArgs().Skip(1).Where(a => a == "--software")) psi.ArgumentList.Add(a);
            System.Diagnostics.Process.Start(psi);
            Close();
        }
        catch (Exception ex) { Vm?.Log(L.T("relance impossible : ") + ex.Message, warning: true); }
    }
}
