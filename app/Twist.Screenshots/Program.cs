using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Threading;
using Avalonia.VisualTree;
using Avalonia.LogicalTree;
using Twist.App;
using Twist.App.Lang;
using Twist.App.Services;
using Twist.App.ViewModels;
using Twist.App.Views;

// Renders each window at its default size to OUTPUT_FOLDER/*.png.
var outDir = args.Length > 0 ? args[0] : "screenshots";
Directory.CreateDirectory(outDir);

// demo settings in a file of their own (never the user's twist.json);
// the CSV journal goes there too
var cfgPath = Path.Combine(Path.GetTempPath(), "twist-screenshots", "twist.json");
if (Directory.Exists(Path.GetDirectoryName(cfgPath))) Directory.Delete(Path.GetDirectoryName(cfgPath)!, true);
Settings Demo()
{
    var c = new Settings();
    c.Station.Callsign = "F6FBB";
    c.Station.Locator = "JN12AX";
    c.Station.KnownCalls = "F5ZZO JN12AX, F1HDI";
    c.Station.LocatorLookup = false;                       // (no network here)
    c.Reports.LogFolder = Path.GetDirectoryName(cfgPath)!;
    c.Current.Name = "2 m";
    var p70 = Settings.CloneOf(c.Current);                 // a second profile: the title bar shows the picker
    p70.Name = "70 cm"; p70.Radio.FrequencyMHz = 432.489; p70.Decoding.CenterHz = 1400;
    c.Profiles.Add(p70);
    c.Profile = "2 m";
    return c;
}

AppBuilder.Configure<App>()
    .UseSkia()
    .UseHeadless(new AvaloniaHeadlessPlatformOptions { UseHeadlessDrawing = false })
    .WithInterFont()
    .SetupWithoutStarting();

void Shot(Window w, string name)
{
    w.Show();
    for (int i = 0; i < 5; i++) { Dispatcher.UIThread.RunJobs(); AvaloniaHeadlessPlatform.ForceRenderTimerTick(); }
    var frame = w.CaptureRenderedFrame();
    frame?.Save(Path.Combine(outDir, name + ".png"));
    Console.WriteLine($"{name}.png  {w.Width}x{w.Height}");
    w.Close();
}

var main = new MainWindowViewModel(Demo(), cfgPath);
Shot(new MainWindow { DataContext = main }, "main");
Shot(new MainWindow { DataContext = main, Width = 860, Height = 1000 }, "main-narrow");
main.Dispose();
// the beacon in its cycle: "prochaine émission" and its time (only then)
{
    var cyc = new MainWindowViewModel(Demo(), cfgPath);
    var w = new MainWindow { DataContext = cyc };
    w.Show();
    cyc.StartCycle();
    for (int i = 0; i < 5; i++) { cyc.TxTick(); Dispatcher.UIThread.RunJobs(); AvaloniaHeadlessPlatform.ForceRenderTimerTick(); }
    if (!cyc.CycleRunning || !cyc.NextTxShown) throw new Exception($"cycle: not running ({string.Join(" | ", cyc.Journal.TakeLast(2))})");
    w.CaptureRenderedFrame()?.Save(Path.Combine(outDir, "main-cycle.png"));
    Console.WriteLine("main-cycle.png");
    cyc.StopCycle();
    cyc.TxTick();
    if (cyc.NextTxShown) throw new Exception("cycle stopped: 'prochaine émission' still shown");
    w.Close();
    cyc.Dispose();
}

// the receiver at work: a recording replayed through it
var replay = args.Length > 1 ? args[1] : null;
if (replay is not null)
{
    var rx = new MainWindowViewModel(Demo(), cfgPath);
    if (args.Length > 3 && args[3] == "fullband") rx.WfBandIndex = 1;   // waterfall over 300 - 3000 Hz
    var w = new MainWindow { DataContext = rx };
    w.Show();
    for (int i = 0; i < 20; i++) { Dispatcher.UIThread.RunJobs(); Thread.Sleep(100); }   // device list
    bool realtime = args.Length > 2 && args[2] == "realtime";   // the waterfall needs real time
    rx.ReplayFile(replay, fast: !realtime);
    var wf = w.GetVisualDescendants().OfType<Twist.App.Controls.WaterfallView>().First();
    bool halfShot = false;
    for (int i = 0; i < 15000 && rx.ReceiverRunning; i++)
    {
        Dispatcher.UIThread.RunJobs();
        rx.ReplayTick();                              // (no timers here)
        if (!halfShot && rx.ReplayPercent >= 50)
        {
            halfShot = true;
            Dispatcher.UIThread.RunJobs();
            AvaloniaHeadlessPlatform.ForceRenderTimerTick();
            w.CaptureRenderedFrame()?.Save(Path.Combine(outDir, "main-replay-progress.png"));
            Console.WriteLine("main-replay-progress.png");
        }
        if (realtime && i % (int)Math.Round(rx.WfLineSeconds / 0.02) == 0) wf.Step();   // a line every LineSeconds
        Thread.Sleep(20);
    }
    for (int i = 0; i < 10; i++) { Dispatcher.UIThread.RunJobs(); Thread.Sleep(100); }
    rx.ReplayTick();
    // the OPERA 05 frame of F5ZZO carries no locator: the one of the list, greyed
    var f5zzo = rx.Spots.FirstOrDefault(x => x.Call == "F5ZZO");
    if (f5zzo is { } z && (z.Loc != "jn12ax" || !z.LocGuessed)) throw new Exception($"F5ZZO: locator {z.Loc} {z.LocGuessed}, jn12ax from the list expected");
    rx.JournalOpen = false;                       // (room for every spot)
    Dispatcher.UIThread.RunJobs();
    AvaloniaHeadlessPlatform.ForceRenderTimerTick();
    Dispatcher.UIThread.RunJobs();
    AvaloniaHeadlessPlatform.ForceRenderTimerTick();
    Dispatcher.UIThread.RunJobs();
    w.CaptureRenderedFrame()?.Save(Path.Combine(outDir, "main-replay.png"));
    Console.WriteLine("main-replay.png");
    void Again(string name)
    {
        for (int i = 0; i < 5; i++) { Dispatcher.UIThread.RunJobs(); AvaloniaHeadlessPlatform.ForceRenderTimerTick(); }
        w.CaptureRenderedFrame()?.Save(Path.Combine(outDir, name + ".png"));
        Console.WriteLine($"{name}.png  {w.Width}x{w.Height}");
    }
    // a signal being received: its label on the waterfall, at the line where
    // it was first seen; said in the spots header, the list does not move
    int row = Enumerable.Range(0, 61).LastOrDefault(r => wf.TimeOfRow(r) is not null);
    Console.WriteLine($"waterfall: {row + 1} lines (in-progress label at the last)");
    if (row > 0 && wf.TimeOfRow(row) is DateTime seen)
    {
        var mark = new Twist.App.Controls.WaterfallMark(1620, seen, L.T("réception en cours…"), true, InProgress: true);
        rx.Marks.Add(mark);
        rx.ReceivingText = L.F("réception en cours · {0:0} Hz · signal manipulé repéré à {1:HH:mm:ss}", 1620, seen);
        rx.ReceivingNow = true;
        wf.InvalidateVisual();                    // (a timer does it in the program)
        Again("main-receiving");
        rx.Marks.Remove(mark);
        rx.ReceivingNow = false; rx.ReceivingText = "";
    }
    else throw new Exception("waterfall: no line to show a signal being received");
    // a ghost: TW6DXY OL30XN, seen by a tester (Wallis & Futuna, a locator
    // in China): the line greyed, "?" and the reason; then deleted
    {
        var why = CallCheck.Doubt("TW6DXY", "OL30XN") ?? throw new Exception("CallCheck: TW6DXY OL30XN not doubtful");
        var ghost = new SpotViewModel
        {
            Start = "08:24:22", Proto = Twist.Native.TwlProto.TwistL, Call = "TW6DXY", Doubt = why,
            Loc = "OL30XN", Snr = "−23", Audio = "1520", Rf = "144,490 520", Distance = "9482 km", Azimuth = "62°",
        };
        rx.Spots.Insert(0, ghost);
        Again("main-doubt");
        int n = rx.Spots.Count;
        rx.DeleteSpotCommand.Execute(ghost);
        if (rx.Spots.Count != n - 1 || rx.Spots.Contains(ghost)) throw new Exception("Supprimer la ligne: still there");
        Console.WriteLine("doubtful spot: checks passed");
    }
    w.Width = 1280; w.Height = 720;              // a lower window: waterfall and spots still share it
    Again("main-replay-small");
    w.Width = 1440; w.Height = 900;
    rx.ShowTech = true;                           // Affichage › Informations techniques
    Again("main-replay-tech");
    rx.ShowTech = false;
    w.Close();
    rx.Dispose();
}
main.Config.Interface.Window.Width = 1440;      // the narrow shot saved its own size
main.Config.Interface.Window.Height = 900;
main.WaterfallOpen = false;
main.JournalOpen = false;
Shot(new MainWindow { DataContext = main }, "main-panels-closed");
// the spots in a window of their own: the waterfall fills the main window
main.WaterfallOpen = true;
main.JournalOpen = true;
main.SpotsDetached = true;
{
    var w = new MainWindow { DataContext = main };
    w.Show();
    for (int i = 0; i < 5; i++) { Dispatcher.UIThread.RunJobs(); AvaloniaHeadlessPlatform.ForceRenderTimerTick(); }
    w.CaptureRenderedFrame()?.Save(Path.Combine(outDir, "main-spots-detached.png"));
    foreach (var o in w.DetachedWindows) o.CaptureRenderedFrame()?.Save(Path.Combine(outDir, "spots-window.png"));
    Console.WriteLine($"main-spots-detached.png, spots-window.png ({w.DetachedWindows.Count} window)");
    w.Close();
    Dispatcher.UIThread.RunJobs();
}
main.SpotsDetached = false;

// detach and put back, every way: the panels must end where they belong
{
    void Run() { for (int i = 0; i < 3; i++) Dispatcher.UIThread.RunJobs(); }
    void Check(bool ok, string what) { if (!ok) throw new Exception("detached parts: " + what); }
    var w = new MainWindow { DataContext = main };
    w.Show(); Run();
    var grid = w.FindControl<Grid>("PanelGrid")!;
    Check(grid.Children.OfType<Border>().Count() == 3 && w.DetachedWindows.Count == 0, "all three in the main window at start");
    main.WaterfallDetached = true; main.JournalDetached = true; Run();
    Check(grid.Children.OfType<Border>().Count() == 1 && w.DetachedWindows.Count == 2, "two detached by the menu");
    // not owned: on Windows, not minimized with the main window (testers, 0.18)
    Check(w.DetachedWindows.All(o => o.Owner is null) && w.OwnedWindows.Count == 0, "windows of their own, not owned");
    foreach (var o in w.DetachedWindows) { o.Width = 1100; o.Height = 480; }
    for (int i = 0; i < 5; i++) { Dispatcher.UIThread.RunJobs(); AvaloniaHeadlessPlatform.ForceRenderTimerTick(); }
    w.DetachedWindows[0].CaptureRenderedFrame()?.Save(Path.Combine(outDir, "waterfall-window.png"));
    w.DetachedWindows[0].Close(); Run();                  // closed by its own button
    Check(!main.WaterfallDetached && grid.Children.OfType<Border>().Count() == 2 && w.DetachedWindows.Count == 1, "closing its window puts it back");
    main.JournalDetached = false; Run();               // the menu, again
    Check(grid.Children.OfType<Border>().Count() == 3 && w.DetachedWindows.Count == 0, "the menu puts it back");
    Check(grid.Children.OfType<Border>().Select(Grid.GetRow).SequenceEqual([0, 2, 4]), "order waterfall, spots, journal");
    main.SpotsDetached = true; Run();
    var spotsWin = w.DetachedWindows.Single();
    w.Close(); Run();                                  // closing the program: still detached next time
    Check(!spotsWin.IsVisible, "closed with the main window");
    Check(main.Config.Interface.SpotsWindow.Detached, "kept detached for the next start");
    main.SpotsDetached = false;
    Console.WriteLine("detached parts: checks passed");
}

// the Balise window follows a change of profile (and writes to the new one)
{
    var vm = new MainWindowViewModel(Demo(), cfgPath);
    vm.Config.Profiles.First(p => p.Name == "70 cm").Beacon.PeriodMinutes = 10;
    var bvm = new BaliseViewModel(vm);
    if (bvm.PeriodMin != 15) throw new Exception($"Balise: {bvm.PeriodMin} min, 15 expected for 2 m");
    vm.CurrentProfile = "70 cm";
    Dispatcher.UIThread.RunJobs();
    if (bvm.PeriodMin != 10) throw new Exception($"Balise: {bvm.PeriodMin} min after the change of profile, 10 expected");
    bvm.PeriodMin = 20;
    var two = vm.Config.Profiles.First(p => p.Name == "2 m").Beacon.PeriodMinutes;
    if (vm.Config.Current.Beacon.PeriodMinutes != 20 || two != 15) throw new Exception("Balise: written to the wrong profile");
    bvm.Detach();
    vm.Dispose();
    Console.WriteLine("Balise and profiles: checks passed");
}

// opening or closing the journal moves the parts: the waterfall keeps its
// lines (0.18: emptied and started again when its centre was not 1500 Hz)
{
    var vm = new MainWindowViewModel(Demo(), cfgPath);
    vm.WfCenter = 1520;
    var w = new MainWindow { DataContext = vm };
    w.Show(); Dispatcher.UIThread.RunJobs();
    var wf = w.GetVisualDescendants().OfType<Twist.App.Controls.WaterfallView>().First();
    wf.Tap = new ToneTap();
    for (int i = 0; i < 20; i++) wf.Step();
    var newest = wf.TimeOfRow(0);
    // one line per pixel of its height, given back to the text above it
    int h0 = (int)Math.Round(wf.Bounds.Height - Twist.App.Controls.WaterfallView.AxisHeight);
    if (wf.Lines != h0 || vm.WfRows != h0 || !vm.WfInfo.Contains($"{h0 * vm.WfLineSeconds:0} s"))
        throw new Exception($"waterfall: {wf.Lines} lines (view model {vm.WfRows}) for {h0} pixels; \"{vm.WfInfo}\"");
    foreach (var open in new[] { !vm.JournalOpen, vm.JournalOpen })
    {
        vm.JournalOpen = open;
        for (int i = 0; i < 3; i++) Dispatcher.UIThread.RunJobs();
        if (newest is null || wf.TimeOfRow(0) != newest || wf.TimeOfRow(19) is null)
            throw new Exception($"waterfall emptied when the journal was {(open ? "opened" : "closed")}");
    }
    // taller window: more lines, those there kept
    int hBefore = wf.Lines;
    w.Height += 200;
    for (int i = 0; i < 5; i++) { Dispatcher.UIThread.RunJobs(); AvaloniaHeadlessPlatform.ForceRenderTimerTick(); }
    if (wf.Lines <= hBefore || wf.TimeOfRow(0) != newest || wf.TimeOfRow(19) is null || vm.WfRows != wf.Lines)
        throw new Exception($"waterfall: {hBefore} then {wf.Lines} lines after the window grew, lines kept: {wf.TimeOfRow(0) == newest}");
    // smaller then taller again: the older lines come back (testers: lost)
    wf.Tap = new ToneTap();                           // (the journal moved it: it took the receiver's tap again)
    for (int i = 0; i < 400; i++) wf.Step();
    int big = wf.Lines;
    var oldest = wf.TimeOfRow(big - 1);
    w.Height -= 300;
    for (int i = 0; i < 5; i++) { Dispatcher.UIThread.RunJobs(); AvaloniaHeadlessPlatform.ForceRenderTimerTick(); }
    int small = wf.Lines;
    w.Height += 300;
    for (int i = 0; i < 5; i++) { Dispatcher.UIThread.RunJobs(); AvaloniaHeadlessPlatform.ForceRenderTimerTick(); }
    // (every line shown again: those lost would be empty; a line or two may
    // have come in meanwhile, the render timer ticking)
    int filled = Enumerable.Range(0, wf.Lines).Count(r => wf.TimeOfRow(r) is not null);
    if (oldest is null || small >= big || wf.Lines != big || filled != big)
        throw new Exception($"waterfall: {big} lines, {small} once smaller, {wf.Lines} again, {filled} of them with a line");
    int Lines() => Enumerable.Range(0, wf.Lines).Count(r => wf.TimeOfRow(r) is not null);
    int before = Lines();
    vm.WfCenter = 1600;                               // the axis moved: the old lines go
    Dispatcher.UIThread.RunJobs();
    wf.Tap = new ToneTap();                           // (moved, the control took the receiver's tap again: none here)
    wf.Step();
    if (Lines() > 3 || before < 20)
        throw new Exception($"waterfall: old lines kept after a change of centre ({Lines()} lines, {before} before)");
    w.Close(); Dispatcher.UIThread.RunJobs();
    vm.Dispose();
    Console.WriteLine($"waterfall kept when the journal opens or closes, and lines with the height ({hBefore} then more): checks passed");
}

// the profile chosen in the title bar is still shown once applied (testers, 0.18)
{
    var vm = new MainWindowViewModel(Demo(), cfgPath);
    var w = new MainWindow { DataContext = vm };
    w.Show(); Dispatcher.UIThread.RunJobs();
    var combo = w.GetLogicalDescendants().OfType<ComboBox>().First(c => ReferenceEquals(c.ItemsSource, vm.ProfileNames));
    combo.SelectedItem = "70 cm";
    for (int i = 0; i < 3; i++) Dispatcher.UIThread.RunJobs();
    // (0.18: the list rebuilt during the choice left the box empty)
    if (vm.Config.Profile != "70 cm" || combo.SelectionBoxItem as string != "70 cm" || vm.CurrentProfile != "70 cm")
        throw new Exception($"profile combo box: {combo.SelectionBoxItem ?? "(nothing)"} shown, {vm.Config.Profile} applied");
    if (w.Title != "TWIST Console  –  " + Twist.App.Services.Report.Version) throw new Exception("title: " + w.Title);
    w.Close(); Dispatcher.UIThread.RunJobs();
    vm.Dispose();
    Console.WriteLine("profile combo box and title: checks passed");
}

// the check marks of the menus change the values they show (and back)
{
    var w = new MainWindow { DataContext = main };
    w.Show(); Dispatcher.UIThread.RunJobs();
    var items = w.GetLogicalDescendants().OfType<MenuItem>().Where(m => m.ToggleType == MenuItemToggleType.CheckBox).ToList();
    if (items.Count != 11) throw new Exception($"menus: {items.Count} check marks, 11 expected");
    string State() => string.Join(",", main.DecodingActive, main.WaterfallOpen, main.JournalOpen, main.WaterfallDetached,
        main.SpotsDetached, main.JournalDetached, main.SearchClassic, main.SearchTwistS, main.SearchTwistL, main.SearchPi4, main.ShowTech);
    var start = State();
    foreach (var m in items)
        for (int k = 0; k < 2; k++)                    // on, then off again (or the reverse)
        {
            var (was, before) = (m.IsChecked, State());
            m.RaiseEvent(new Avalonia.Interactivity.RoutedEventArgs(MenuItem.ClickEvent));
            for (int i = 0; i < 3; i++) Dispatcher.UIThread.RunJobs();
            if (m.IsChecked == was || State().Equals(before)) throw new Exception($"menu « {m.Header} »: the click changed nothing");
        }
    if (!State().Equals(start)) throw new Exception("menus: not back to the start");
    Console.WriteLine("menus: check marks passed");
    w.Close(); Dispatcher.UIThread.RunJobs();
}
// PI4: the real recording of F5ZZO (its callsign, then "/JN12AX" the next
// minute): the locator message given to the callsign of the minute before
{
    var pi4File = Path.Combine("tests", "real", "pi4_f5zzo.wav");
    if (!File.Exists(pi4File)) pi4File = Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "..", "tests", "real", "pi4_f5zzo.wav");
    if (File.Exists(pi4File))
    {
        var vm = new MainWindowViewModel(Demo(), cfgPath);
        vm.SearchPi4 = true;
        var w = new MainWindow { DataContext = vm };
        w.Show();
        for (int i = 0; i < 20; i++) { Dispatcher.UIThread.RunJobs(); Thread.Sleep(50); }
        vm.ReplayFile(Path.GetFullPath(pi4File), fast: true);
        // the frame in progress: at the PI4 carrier, never at its 4th tone
        // (1384 Hz, in the OOK zone); a view of it during the second frame
        // (every label as it is added: in a fast replay, one can come and go
        // between two looks)
        bool pi4Live = false, livePicture = false;
        var ookLive = new List<double>();
        vm.Marks.CollectionChanged += (_, e) =>
        {
            foreach (var m in e.NewItems?.OfType<Twist.App.Controls.WaterfallMark>() ?? [])
                if (!m.InProgress) { }
                else if (m.Pi4 && Math.Abs(m.FreqHz - 798) < 12) pi4Live = true;
                else if (!m.Pi4) ookLive.Add(m.FreqHz);
        };
        for (int i = 0; i < 15000 && vm.ReceiverRunning; i++)
        {
            Dispatcher.UIThread.RunJobs(); vm.ReplayTick(); Thread.Sleep(20);
            if (!livePicture && vm.Spots.Count == 1 && vm.Marks.Any(m => m.InProgress && m.Pi4))
            {
                livePicture = true;
                vm.JournalOpen = false;
                for (int k = 0; k < 5; k++) { Dispatcher.UIThread.RunJobs(); AvaloniaHeadlessPlatform.ForceRenderTimerTick(); }
                w.CaptureRenderedFrame()?.Save(Path.Combine(outDir, "main-pi4-live.png"));
            }
        }
        for (int i = 0; i < 10; i++) { Dispatcher.UIThread.RunJobs(); Thread.Sleep(50); }
        if (!pi4Live || ookLive.Count > 0)
            throw new Exception($"PI4 in progress: at the carrier {pi4Live}, OOK at {string.Join(" ", ookLive.Distinct().Select(f => f.ToString("0")))}");
        var pi4 = vm.Spots.Where(x => x.Proto == Twist.Native.TwlProto.Pi4).ToList();
        // the callsign given to the locator: in small letters, greyed; the
        // waterfall label of the locator message: what it holds
        if (pi4.Count != 2 || !pi4.Any(x => x.Call == "F5ZZO" && !x.CallGuessed)
            || !pi4.Any(x => x.Loc == "JN12AX" && x.Call == "f5zzo" && x.CallGuessed && x.CallTip is not null)
            || !vm.Marks.Any(m => m.Label.StartsWith("/JN12AX")) || vm.Marks.Any(m => m.Label.StartsWith("f5zzo")))
            throw new Exception("PI4: " + string.Join(" | ", pi4.Select(x => $"{x.Call} {x.Loc} {x.CallGuessed}"))
                                + " / " + string.Join(" | ", vm.Marks.Select(m => m.Label)));
        // both zones on the waterfall, the band shown around them
        var wfv = w.GetVisualDescendants().OfType<Twist.App.Controls.WaterfallView>().First();
        if (!wfv.ShowZone || !wfv.ShowPi4Zone || vm.WfBand != (550, 1800))
            throw new Exception($"PI4 zones: {wfv.ShowZone} {wfv.ShowPi4Zone} {vm.WfBand}");
        vm.JournalOpen = false;
        for (int i = 0; i < 5; i++) { Dispatcher.UIThread.RunJobs(); AvaloniaHeadlessPlatform.ForceRenderTimerTick(); }
        w.CaptureRenderedFrame()?.Save(Path.Combine(outDir, "main-pi4.png"));
        Console.WriteLine("PI4: checks passed (main-pi4.png)");
        // Garder les 2 dernières minutes (Ctrl+S): the audio of that replay, at 12 kHz
        var keep = vm.SaveLastCommand.ExecuteAsync(null);
        for (int i = 0; i < 300 && !keep.IsCompleted; i++) { Dispatcher.UIThread.RunJobs(); Thread.Sleep(10); }
        if (vm.LastRecording is not { } kept || !File.Exists(kept) || Twist.App.Services.Wav.ReadMono(kept) is not { fs: 12000 } kw
            || kw.samples.Length < 80 * 12000)
            throw new Exception("last minutes not kept: " + vm.LastRecording);
        Console.WriteLine($"last minutes kept: {Path.GetFileName(kept)}");
        File.Delete(kept);
        // the badge of a continuous recording (for the guide: live sources only)
        vm.Recording = true;
        vm.RecordingText = "● REC 3:12";
        for (int i = 0; i < 5; i++) { Dispatcher.UIThread.RunJobs(); AvaloniaHeadlessPlatform.ForceRenderTimerTick(); }
        w.CaptureRenderedFrame()?.Save(Path.Combine(outDir, "main-recording.png"));
        vm.Recording = false;
        w.Close();
        vm.Dispose();

        // the other way round: the locator first (61.4 s of the recording
        // put at 6.4 s), then the callsign one minute later (at 66.4 s):
        // the locator line waits, then gets the callsign
        var (x, fs) = Twist.App.Services.Wav.ReadMono(pi4File);
        var y = new float[95 * fs];
        Array.Copy(x, 55 * fs, y, 0, 32 * fs);                // /JN12AX at 6.4 s
        Array.Copy(x, 25 * fs, y, 32 * fs, 33 * fs);          // carrier, CW
        Array.Copy(x, 0, y, 65 * fs, 30 * fs);                // F5ZZO at 66.4 s
        var rev = Path.Combine(Path.GetTempPath(), "pi4_reversed.wav");
        Twist.App.Services.Wav.WriteMono16(rev, y, fs);
        var vm2 = new MainWindowViewModel(Demo(), cfgPath);
        vm2.SearchPi4 = true;
        // (Réglages › Interface: a frame in progress labelled with its frequency)
        vm2.Config.Interface.Waterfall.LiveFrequency = true;
        var liveLabels = new List<string>();
        vm2.Marks.CollectionChanged += (_, e) =>
        {
            foreach (var m in e.NewItems?.OfType<Twist.App.Controls.WaterfallMark>() ?? []) if (m.InProgress) liveLabels.Add(m.Label);
        };
        var w2 = new MainWindow { DataContext = vm2 };
        w2.Show();
        for (int i = 0; i < 20; i++) { Dispatcher.UIThread.RunJobs(); Thread.Sleep(50); }
        vm2.ReplayFile(rev, fast: true);
        string? waiting = null;
        for (int i = 0; i < 15000 && vm2.ReceiverRunning; i++)
        {
            Dispatcher.UIThread.RunJobs(); vm2.ReplayTick(); Thread.Sleep(20);
            if (waiting is null && vm2.Spots.Count == 1) waiting = vm2.Spots[0].Call;
        }
        for (int i = 0; i < 10; i++) { Dispatcher.UIThread.RunJobs(); Thread.Sleep(50); }
        var p2 = vm2.Spots.ToList();
        if (waiting != "—" || p2.Count != 2 || !p2.Any(s => s.Loc == "JN12AX" && s.Call == "f5zzo" && s.CallGuessed && (s.LocTip ?? "").Contains("après"))
            || !p2.Any(s => s.Call == "F5ZZO" && !s.CallGuessed))
            throw new Exception($"PI4 reversed: first {waiting}; " + string.Join(" | ", p2.Select(s => $"{s.Call} {s.Loc} {s.CallGuessed} {s.LocTip}")));
        if (liveLabels.Count == 0 || !liveLabels.All(l => System.Text.RegularExpressions.Regex.IsMatch(l, @"^PI4 · 79\d Hz$")))
            throw new Exception("PI4 in progress, labelled with its frequency: " + string.Join(" | ", liveLabels.Distinct()));
        Console.WriteLine($"PI4 reversed: checks passed (in progress: {liveLabels[0]})");
        w2.Close();
        vm2.Dispose();
        File.Delete(rev);
    }
    else Console.WriteLine("PI4: tests/real/pi4_f5zzo.wav not found, skipped");
}
// a new version announced (latest.json from a fake server): the banner
{
    var uvm = new MainWindowViewModel(Demo(), cfgPath);
    uvm.Config.Interface.UpdateUrl = "https://update.example.org/twist/latest.json";
    uvm.UpdateService.Http = new System.Net.Http.HttpClient(new FakeUpdateServer());
    var w = new MainWindow { DataContext = uvm };
    w.Show();
    var job = uvm.CheckUpdateNowCommand.ExecuteAsync(null);
    for (int i = 0; i < 200 && !job.IsCompleted; i++) { Dispatcher.UIThread.RunJobs(); Thread.Sleep(10); }
    if (!uvm.UpdateBanner || !uvm.UpdateText.Contains("99.0.0") || !uvm.UpdateNotes.Contains("update check"))
        throw new Exception($"update banner: {uvm.UpdateBanner} \"{uvm.UpdateText}\"");
    for (int i = 0; i < 5; i++) { Dispatcher.UIThread.RunJobs(); AvaloniaHeadlessPlatform.ForceRenderTimerTick(); }
    // the window that comes with it (testers: the banner alone is not seen)
    var uw = w.OwnedWindows.OfType<UpdateWindow>().FirstOrDefault();
    if (uw is null) throw new Exception("update: no window for the new version");
    w.CaptureRenderedFrame()?.Save(Path.Combine(outDir, "main-update.png"));
    for (int i = 0; i < 5; i++) { Dispatcher.UIThread.RunJobs(); AvaloniaHeadlessPlatform.ForceRenderTimerTick(); }
    uw.CaptureRenderedFrame()?.Save(Path.Combine(outDir, "update-window.png"));
    uw.Close(); Dispatcher.UIThread.RunJobs();
    // up to date: asked for, a window says so
    uvm.UpdateService.Http = new System.Net.Http.HttpClient(new FakeUpdateServer("0.0.1"));
    uvm.UpdateBanner = false;
    job = uvm.CheckUpdateNowCommand.ExecuteAsync(null);
    for (int i = 0; i < 200 && !job.IsCompleted; i++) { Dispatcher.UIThread.RunJobs(); Thread.Sleep(10); }
    var none = w.OwnedWindows.OfType<UpdateWindow>().FirstOrDefault();
    if (none is null || none.FindControl<TextBlock>("Body")?.Text?.Contains("dernière version") != true)
        throw new Exception("update: no answer when up to date");
    for (int i = 0; i < 5; i++) { Dispatcher.UIThread.RunJobs(); AvaloniaHeadlessPlatform.ForceRenderTimerTick(); }
    none.CaptureRenderedFrame()?.Save(Path.Combine(outDir, "update-none.png"));
    none.Close();
    w.Close(); Dispatcher.UIThread.RunJobs();
    uvm.Dispose();
    Console.WriteLine("update banner and windows: checks passed (main-update.png, update-window.png, update-none.png)");
}
Shot(new BaliseWindow { DataContext = new BaliseViewModel(main) }, "balise");
// the beacon of a PI4 station: "Séquence PI4 (1 min)" - PI4 frame, CW and
// carrier at the PI4 frequency, every minute; callsign and /locator one
// minute out of two
{
    var pvm = new MainWindowViewModel(Demo(), cfgPath);
    var bv = new BaliseViewModel(pvm);
    bv.Pi4SequenceCommand.Execute(null);
    var b = pvm.Config.Current.Beacon;
    var even = new DateTime(2026, 11, 6, 10, 0, 0, DateTimeKind.Utc);      // minute 29 870 760 since 1970: even
    var s0 = pvm.SequenceSteps(out var pr0, even);
    var s1 = pvm.SequenceSteps(out var pr1, even.AddMinutes(1));
    double total = Twist.App.Services.Beacon.TotalSeconds(s0);
    if (bv.Steps.Count != 3 || !bv.Steps[0].IsPi4Frame || !bv.Steps[1].IsCw || !bv.Steps[1].AtPi4 || !bv.Steps[2].IsCarrier || !bv.Steps[2].AtPi4
        || b.PeriodMinutes != 1 || b.StartSecond != 0 || bv.Warning != "" || pr0 is not null || pr1 is not null
        || Math.Abs(total - BaliseViewModel.Pi4MinuteSeconds) > 0.11
        || s0[0].Proto != Twist.Native.TwlProto.Pi4 || s0[0].Call != pvm.Callsign || s0[0].ToneHz != 800
        || s1[0].Call != "" || s1[0].Loc != pvm.Locator || s0[1].Kind != Twist.Native.TwlStepKind.Cw || s0[2].ToneHz != 800)
        throw new Exception($"PI4 beacon: {bv.Steps.Count} steps, {total:0.00} s, \"{bv.Warning}\" {pr0} {pr1} {s0[0].Call}/{s1[0].Call}/{s1[0].Loc}");
    var bw = new BaliseWindow { DataContext = bv, Height = 1000 };
    Shot(bw, "balise-pi4");
    Console.WriteLine($"PI4 beacon: checks passed ({total:0.0} s: PI4 + CW {Twist.App.Services.Beacon.StepSeconds(s0[1]):0.0} s + carrier)");
    pvm.Dispose();
}
var demo = Demo();
demo.Current.Audio.Input = "CABLE Output (VB-Audio Virtual Cable) (Windows WASAPI)";
demo.Current.Decoding.ChainDelaySeconds = 1;
demo.Station.LocatorLookup = true;                         // (as by default)
demo.Current.Radio.Cat = "hamlib"; demo.Current.Radio.RigModel = 1035; demo.Current.Radio.Rig = "Yaesu FT-991";
demo.Current.Radio.Port = "COM4"; demo.Current.Radio.Ptt = "CAT"; demo.Current.Radio.FreqFromCat = true;
demo.Reports.Fbbspots = true; demo.Reports.FbbspotsKey = "fbb_0123456789abcdef0123456789abcdef01234567";
demo.Reports.Mqtt = false; demo.Reports.MqttHost = "mqtt.example.org"; demo.Reports.MqttUser = "f6fbb"; demo.Reports.MqttTopic = "fbbspots/RadioServer/spot";
// TWIST_ALL_TABS=1: every tab (docs/guide)
var tabs = Environment.GetEnvironmentVariable("TWIST_ALL_TABS") == "1"
    ? new[] { (0, "settings"), (1, "settings-audio"), (2, "settings-radio"), (3, "settings-decoding"), (4, "settings-beacon"), (5, "settings-reports"), (6, "settings-interface") }
    : new[] { (0, "settings"), (2, "settings-radio"), (3, "settings-decoding"), (5, "settings-reports") };
foreach (var (tab, name) in tabs)
    Shot(new SettingsWindow
    {
        DataContext = new SettingsViewModel(demo, @"C:\Users\jpr\AppData\Roaming\TWIST\twist.json",
            ["Microphone (USB Audio CODEC) (Windows WASAPI)", "CABLE Output (VB-Audio Virtual Cable) (Windows WASAPI)"],
            ["Haut-parleurs (USB Audio CODEC) (Windows WASAPI)"]) { Tab = tab }
    }, name);
Shot(new ToolsWindow { DataContext = new ToolsViewModel() }, "tools");
// WAV de test: a PI4 file written, then read back by the decoder
{
    var tv = new ToolsViewModel { WavProto = "PI4", WavSnr = -18 };
    var tw = new ToolsWindow { DataContext = tv };
    tw.Show();
    var tc = tw.GetVisualDescendants().OfType<TabControl>().First();
    tc.SelectedIndex = tc.Items.Count - 1;
    var wav = Path.Combine(Path.GetTempPath(), "twist-test-pi4.wav");
    var job = tv.WavWrite(wav);
    for (int i = 0; i < 1000 && !job.IsCompleted; i++) { Dispatcher.UIThread.RunJobs(); Thread.Sleep(20); }
    if (tv.WavFreq != 800 || !tv.WavResult.Contains("PI4 F6FBB") || !File.Exists(wav))
        throw new Exception($"WAV de test: {tv.WavFreq} Hz, \"{tv.WavResult}\"");
    for (int i = 0; i < 5; i++) { Dispatcher.UIThread.RunJobs(); AvaloniaHeadlessPlatform.ForceRenderTimerTick(); }
    tw.CaptureRenderedFrame()?.Save(Path.Combine(outDir, "tools-wav.png"));
    Console.WriteLine("WAV de test: checks passed (tools-wav.png) — " + tv.WavResult);
    tw.Close();
    File.Delete(wav);
}
// Aide › Diagnostic fbbspots, against a fake server (as the guide shows it:
// the radio's frequency unknown, the rest good)
{
    var diag = new DiagWindow(() => Twist.App.Services.FbbspotsDiag.RunAsync(
        new(true, "https://spots.f6fbb.org", "fbb_0123456789abcdef0123456789abcdef01234567", "F6FBB", true, null, null),
        new System.Net.Http.HttpClient(new FakeFbbspots()),
        _ => Task.FromResult(new[] { System.Net.IPAddress.Parse("203.0.113.25") })));
    diag.Show();
    for (int i = 0; i < 200 && (diag.Busy || diag.FindControl<ItemsControl>("Lines")?.ItemsSource is null); i++) { Dispatcher.UIThread.RunJobs(); Thread.Sleep(10); }
    var rows = (diag.FindControl<ItemsControl>("Lines")?.ItemsSource as System.Collections.IList)?.Count ?? 0;
    if (rows < 6) throw new Exception($"fbbspots diagnostic: {rows} lines");
    for (int i = 0; i < 5; i++) { Dispatcher.UIThread.RunJobs(); AvaloniaHeadlessPlatform.ForceRenderTimerTick(); }
    diag.CaptureRenderedFrame()?.Save(Path.Combine(outDir, "diag-fbbspots.png"));
    diag.Close();
    Console.WriteLine($"fbbspots diagnostic: checks passed ({rows} lines, diag-fbbspots.png)");
}
Shot(new AboutWindow(), "about");
Shot(new AboutWindow { CreditsOpen = true }, "about-credits");
Shot(ProfileWindow.New("2 m", (_, _) => null), "profile-new");
Shot(new KeyRequestWindow(new SettingsViewModel(Demo(), "twist.json", [], [])), "key-request");
foreach (var (ok, name) in new[] { (true, "key-request-sent"), (false, "key-request-refused") })
{
    var kw = new KeyRequestWindow(new SettingsViewModel(Demo(), "twist.json", [], []));
    kw.ShowAnswer(ok ? "La clé a été envoyée à f6fbb@example.org. Collez ensuite la clé reçue dans « Clé fbbspots de la station »."
               : "F6FBB est déjà inscrit avec une autre adresse : contactez l'administrateur.", ok);   // no-tr
    Shot(kw, name);
}
Shot(ProfileWindow.Message("Langue", "L'interface sera en anglais au prochain démarrage du programme.", "The interface will be in English from the next start of the program.",   // no-tr
                           L.T("Relancer TWIST Console"), L.T("Plus tard")), "language-notice");

// the light theme (Réglages › Interface)
var lightCfg = Demo();
lightCfg.Interface.Theme = "light";                     // the window applies its settings' theme
var lightVm = new MainWindowViewModel(lightCfg, cfgPath);
Shot(new MainWindow { DataContext = lightVm }, "main-light");
Shot(new BaliseWindow { DataContext = new BaliseViewModel(lightVm) }, "balise-light");
Shot(new SettingsWindow { DataContext = new SettingsViewModel(demo, "twist.json", [], []) { Tab = 2 } }, "settings-light");
MainWindowViewModel.ApplyTheme("dark");

// in English (Réglages › Interface › Langue)
Twist.App.Lang.L.Set("en");
Twist.App.Lang.L.HookControls();
var enVm = new MainWindowViewModel(Demo(), cfgPath);
Shot(new MainWindow { DataContext = enVm }, "main-en");
Shot(new BaliseWindow { DataContext = new BaliseViewModel(enVm) }, "balise-en");
Shot(new SettingsWindow { DataContext = new SettingsViewModel(demo, "twist.json", [], []) { Tab = 2 } }, "settings-en");

// a tone at 1520 Hz in a little noise, for the waterfall without a sound card
sealed class ToneTap : Twist.App.Controls.IAudioTap
{
    readonly Random _r = new(1);
    long _n;
    public bool Active => true;
    public int Rate => 12000;
    public int Last(float[] buf)
    {
        for (int i = 0; i < buf.Length; i++, _n++)
            buf[i] = (float)(0.1 * Math.Sin(2 * Math.PI * 1520 * _n / 12000.0) + 0.01 * (_r.NextDouble() - 0.5));
        return buf.Length;
    }
}

// latest.json of a version 99.0.0, for the update banner
sealed class FakeUpdateServer(string version = "99.0.0") : System.Net.Http.HttpMessageHandler
{
    protected override Task<System.Net.Http.HttpResponseMessage> SendAsync(System.Net.Http.HttpRequestMessage r, CancellationToken ct) =>
        Task.FromResult(new System.Net.Http.HttpResponseMessage(System.Net.HttpStatusCode.OK)
        {
            Content = new System.Net.Http.StringContent("""
                { "version": "VERSION", "date": "2026-11-10", "notes": "### Added\n- update check\n- a window for it\n\n### Fixed\n- the banner was hardly seen", "page": "https://update.example.org/twist/",
                  "windows": { "setup": { "file": "TWIST-Console-99.0.0-setup.exe", "size": 40000000,
                                          "sha256": "0000000000000000000000000000000000000000000000000000000000000000" } } }
                """.Replace("VERSION", version)),
        });
}

// spots.f6fbb.org with /ping and /check, for the diagnostic's picture
sealed class FakeFbbspots : System.Net.Http.HttpMessageHandler
{
    protected override Task<System.Net.Http.HttpResponseMessage> SendAsync(System.Net.Http.HttpRequestMessage r, CancellationToken ct)
    {
        var body = r.RequestUri!.AbsolutePath switch
        {
            "/ping" => """{"service":"fbbspots","version":"1.3","time":"2026-11-11T17:00:00Z"}""",
            "/check" => """{"callsign":"F6FBB","status":"active","last_spot":"2026-11-11 16:58:02"}""",
            _ => "{}",
        };
        var a = new System.Net.Http.HttpResponseMessage(System.Net.HttpStatusCode.OK) { Content = new System.Net.Http.StringContent(body) };
        a.Headers.Date = DateTimeOffset.UtcNow;
        return Task.FromResult(a);
    }
}
