using System.Collections;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;

namespace Twist.App.Controls;

// Where the waterfall reads its audio: the newest samples of the
// capture (libtwist ring buffer), mono, at Rate.
public interface IAudioTap
{
    bool Active { get; }
    int Rate { get; }
    int Last(float[] buf);
}

// A label on the waterfall: a decoded frame, at its frequency, at the
// moment it ended.
// InProgress: a keyed signal being received (dashed), at the line where it was first seen.
public sealed record WaterfallMark(double FreqHz, DateTime TimeUtc, string Label, bool Classic, bool InProgress = false, bool Pi4 = false);

// Scrolling spectrum of the audio band, newest line at the top.
// Every LineSeconds it takes the newest 0.68 s from Tap (about five chips),
// computes their spectrum (Hann window, FFT) and adds one line; the colour of a point
// is its level above the noise floor of its line, so the picture stays
// readable whatever the input level. Gain moves the colours up or down,
// Contrast narrows the range shown (40 dB down to 10 dB).
// Shown: Span (1000 Hz) around the zones, or a fixed band (FixedLo -
// FixedHi: the whole audio band 300 - 2700 Hz, nothing useful above in
// SSB, or 200 - 2000 Hz).
// One line per pixel of its height (a taller window shows more time);
// the lines already there are kept when its size changes.
// Marks: the search zones (dashed), decoded frames, a thin dashed line
// every MinuteLines minutes (UTC) with its time, and a frequency axis at
// the top, where the lines come in, a tick every 50 Hz.
public class WaterfallView : Control
{
    public static readonly StyledProperty<IAudioTap?> TapProperty = AvaloniaProperty.Register<WaterfallView, IAudioTap?>(nameof(Tap));
    public static readonly StyledProperty<double> CenterProperty = AvaloniaProperty.Register<WaterfallView, double>(nameof(Center), 1500);
    public static readonly StyledProperty<double> HalfWidthProperty = AvaloniaProperty.Register<WaterfallView, double>(nameof(HalfWidth), 200);
    public static readonly StyledProperty<double> SpanProperty = AvaloniaProperty.Register<WaterfallView, double>(nameof(Span), 1000);
    public static readonly StyledProperty<double> FixedLoProperty = AvaloniaProperty.Register<WaterfallView, double>(nameof(FixedLo));
    public static readonly StyledProperty<double> FixedHiProperty = AvaloniaProperty.Register<WaterfallView, double>(nameof(FixedHi));
    public static readonly StyledProperty<int> MinuteLinesProperty = AvaloniaProperty.Register<WaterfallView, int>(nameof(MinuteLines), 1);
    // the number of lines, for the text above (seconds shown): bound one way to the view model
    public static readonly DirectProperty<WaterfallView, int> LinesProperty =
        AvaloniaProperty.RegisterDirect<WaterfallView, int>(nameof(Lines), o => o.Lines, (o, v) => { },
            defaultBindingMode: Avalonia.Data.BindingMode.OneWayToSource);
    public static readonly StyledProperty<double> GainProperty = AvaloniaProperty.Register<WaterfallView, double>(nameof(Gain), 50);
    public static readonly StyledProperty<double> ContrastProperty = AvaloniaProperty.Register<WaterfallView, double>(nameof(Contrast), 40);
    public static readonly StyledProperty<double> LineSecondsProperty = AvaloniaProperty.Register<WaterfallView, double>(nameof(LineSeconds), 0.2);
    public static readonly StyledProperty<IEnumerable?> MarksProperty = AvaloniaProperty.Register<WaterfallView, IEnumerable?>(nameof(Marks));
    // the zone of the OOK frames (Center +/- HalfWidth, cyan) and that of
    // PI4 (Pi4Center +/- Pi4HalfWidth, green), each shown when searched
    public static readonly StyledProperty<bool> ShowZoneProperty = AvaloniaProperty.Register<WaterfallView, bool>(nameof(ShowZone), true);
    public static readonly StyledProperty<bool> ShowPi4ZoneProperty = AvaloniaProperty.Register<WaterfallView, bool>(nameof(ShowPi4Zone));
    public static readonly StyledProperty<double> Pi4CenterProperty = AvaloniaProperty.Register<WaterfallView, double>(nameof(Pi4Center), 800);
    public static readonly StyledProperty<double> Pi4HalfWidthProperty = AvaloniaProperty.Register<WaterfallView, double>(nameof(Pi4HalfWidth), 150);
    public bool ShowZone { get => GetValue(ShowZoneProperty); set => SetValue(ShowZoneProperty, value); }
    public bool ShowPi4Zone { get => GetValue(ShowPi4ZoneProperty); set => SetValue(ShowPi4ZoneProperty, value); }
    public double Pi4Center { get => GetValue(Pi4CenterProperty); set => SetValue(Pi4CenterProperty, value); }
    public double Pi4HalfWidth { get => GetValue(Pi4HalfWidthProperty); set => SetValue(Pi4HalfWidthProperty, value); }

    public IAudioTap? Tap { get => GetValue(TapProperty); set => SetValue(TapProperty, value); }
    public double Center { get => GetValue(CenterProperty); set => SetValue(CenterProperty, value); }
    public double HalfWidth { get => GetValue(HalfWidthProperty); set => SetValue(HalfWidthProperty, value); }
    public double Span { get => GetValue(SpanProperty); set => SetValue(SpanProperty, value); }
    // a fixed band (FixedLo..FixedHi) instead of Span around the zones; 0, 0: around the zones
    public double FixedLo { get => GetValue(FixedLoProperty); set => SetValue(FixedLoProperty, value); }
    public double FixedHi { get => GetValue(FixedHiProperty); set => SetValue(FixedHiProperty, value); }
    public const double FullBandMin = 300, FullBandMax = 2700;
    // a dashed line every MinuteLines minutes (0: none)
    public int MinuteLines { get => GetValue(MinuteLinesProperty); set => SetValue(MinuteLinesProperty, value); }
    int _lines = DefaultRows;
    public int Lines { get => _lines; private set => SetAndRaise(LinesProperty, ref _lines, value); }
    public double Gain { get => GetValue(GainProperty); set => SetValue(GainProperty, value); }
    public double Contrast { get => GetValue(ContrastProperty); set => SetValue(ContrastProperty, value); }
    public double LineSeconds { get => GetValue(LineSecondsProperty); set => SetValue(LineSecondsProperty, value); }
    public IEnumerable? Marks { get => GetValue(MarksProperty); set => SetValue(MarksProperty, value); }

    static WaterfallView() =>
        AffectsRender<WaterfallView>(CenterProperty, HalfWidthProperty, SpanProperty, FixedLoProperty, FixedHiProperty, MarksProperty,
                                     ShowZoneProperty, ShowPi4ZoneProperty, Pi4CenterProperty, Pi4HalfWidthProperty, MinuteLinesProperty);

    public const double AxisHeight = 20;
    // Length of the analysed audio. A short one (one chip, 0.17 s) shows
    // the keying but spreads each signal over ~25 Hz and splits the noise
    // into 6 Hz blocks; the lines (0.2 s or more) cannot follow the chips
    // anyway, so a longer one gives thin traces (~8 Hz, the keying
    // sidebands) and a finer, darker noise; frames blur by ~0.7 s.
    public const double FftSeconds = 0.6;
    // 1 Hz per column at 1000 Hz, 2.4 Hz on the whole band; lines: one per
    // pixel of the height (60 s at 0.2 s per line for 300 pixels)
    // Rows: the lines shown; Cap: the lines kept (it only grows: shrinking
    // the window then enlarging it again shows the older lines again)
    const int Cols = 1000, DefaultRows = 300, MinRows = 40, MaxRows = 2000;
    int Rows = DefaultRows, Cap = DefaultRows;

    WriteableBitmap _bmp = new(new PixelSize(Cols, DefaultRows), new Vector(96, 96), PixelFormat.Bgra8888, AlphaFormat.Opaque);
    readonly DispatcherTimer _timer = new();
    // UTC time of each line, newest (top) first; MinValue: no line yet.
    // Kept per line because the speed can change while lines are shown.
    DateTime[] _times = new DateTime[DefaultRows];
    float[] _buf = Array.Empty<float>();
    double[] _re = Array.Empty<double>(), _im = Array.Empty<double>(), _win = Array.Empty<double>();
    readonly double[] _row = new double[Cols];
    double _floor = double.NaN;               // smoothed noise floor, dB

    public WaterfallView()
    {
        Clear();
        _timer.Interval = TimeSpan.FromSeconds(0.2);
        _timer.Tick += (_, _) => Step();
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        _timer.Start();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        _timer.Stop();
        base.OnDetachedFromVisualTree(e);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == LineSecondsProperty)
            _timer.Interval = TimeSpan.FromSeconds(Math.Clamp(LineSeconds, 0.05, 5));
        // (a change of the frequency axis empties the picture, but only when
        // the picture is next drawn or added to: moved in the window, the
        // control loses its data context for a moment and its centre goes
        // back to 1500 Hz - testers, 0.18: emptied by opening the journal)
    }

    // one line per pixel: as many lines shown as the height has pixels; a
    // bigger picture when it is taller than ever, the lines copied into it
    protected override void OnSizeChanged(SizeChangedEventArgs e)
    {
        base.OnSizeChanged(e);
        Resize((int)Math.Round(e.NewSize.Height - AxisHeight));
    }

    void Resize(int rows)
    {
        rows = Math.Clamp(rows, MinRows, MaxRows);
        if (rows == Rows) return;
        Rows = rows;
        Lines = rows;
        InvalidateVisual();
        if (rows <= Cap) return;
        var bmp = new WriteableBitmap(new PixelSize(Cols, rows), new Vector(96, 96), PixelFormat.Bgra8888, AlphaFormat.Opaque);
        var times = new DateTime[rows];
        Array.Fill(times, DateTime.MinValue);
        int keep = Cap;
        using (var src = _bmp.Lock())
        using (var dst = bmp.Lock())
        unsafe
        {
            uint* p = (uint*)dst.Address;
            for (int i = 0; i < dst.RowBytes / 4 * rows; i++) p[i] = 0xFF06101Cu;
            for (int r = 0; r < keep; r++)
                Buffer.MemoryCopy((byte*)src.Address + (long)r * src.RowBytes, (byte*)dst.Address + (long)r * dst.RowBytes, dst.RowBytes, Cols * 4);
        }
        Array.Copy(_times, times, keep);
        _bmp.Dispose();
        _bmp = bmp;
        _times = times;
        Cap = rows;
    }

    // the axis the lines shown were made with
    double _axisMin = double.NaN, _axisMax = double.NaN;
    void SameAxis()
    {
        if (FMin == _axisMin && FMax == _axisMax) return;
        if (!double.IsNaN(_axisMin)) Clear();       // the frequency axis moved: old lines are wrong
        _axisMin = FMin; _axisMax = FMax;
    }

    double FMin => Range(FixedLo, FixedHi, Center, Span, ShowZone, HalfWidth, ShowPi4Zone, Pi4Center, Pi4HalfWidth).lo;
    double FMax => Range(FixedLo, FixedHi, Center, Span, ShowZone, HalfWidth, ShowPi4Zone, Pi4Center, Pi4HalfWidth).hi;

    // The band shown: a fixed band (fixedLo - fixedHi), or Span around the
    // OOK zone; with the PI4 zone, around both zones (100 Hz beyond each,
    // at least Span wide), or around the PI4 zone alone. (Also the text
    // above the waterfall, MainWindowViewModel.WfInfo.)
    public static (double lo, double hi) Range(double fixedLo, double fixedHi, double center, double span, bool ook, double halfWidth,
                                               bool pi4, double pi4Center, double pi4Half)
    {
        if (fixedHi > fixedLo) return (fixedLo, fixedHi);
        if (!pi4) return (center - span / 2, center + span / 2);
        double lo = pi4Center - pi4Half - 100, hi = pi4Center + pi4Half + 100;
        if (ook) { lo = Math.Min(lo, center - halfWidth - 100); hi = Math.Max(hi, center + halfWidth + 100); }
        if (hi - lo < span) { double c = (lo + hi) / 2; lo = c - span / 2; hi = c + span / 2; }
        return (Math.Round(lo), Math.Round(hi));
    }
    double Band => FMax - FMin;

    void Clear()
    {
        using var fb = _bmp.Lock();
        unsafe
        {
            uint* p = (uint*)fb.Address;
            for (int i = 0; i < fb.RowBytes / 4 * Cap; i++) p[i] = 0xFF06101Cu;
        }
        Array.Fill(_times, DateTime.MinValue);
    }

    // adds one line from the newest audio (called by the timer; public
    // for the screenshot tool, whose headless platform has no real timers)
    public void Step()
    {
        var tap = Tap;
        if (tap is null || !tap.Active || !IsEffectivelyVisible) return;
        int fs = tap.Rate;
        if (fs <= 0) return;
        int n = 1;                                   // FFT over ~0.68 s (about 5 chips):
        while (n < fs * FftSeconds) n <<= 1;         // 8192 at 12 kHz, bins of 1.5 Hz
        if (_buf.Length != n)
        {
            _buf = new float[n]; _re = new double[n]; _im = new double[n]; _win = new double[n];
            for (int i = 0; i < n; i++) _win[i] = 0.5 - 0.5 * Math.Cos(2 * Math.PI * i / (n - 1));
        }
        if (tap.Last(_buf) < n) return;
        SameAxis();
        for (int i = 0; i < n; i++) { _re[i] = _buf[i] * _win[i]; _im[i] = 0; }
        Services.Dsp.Fft(_re, _im);

        // each column: the strongest bin under it
        double binHz = (double)fs / n;
        for (int c = 0; c < Cols; c++)
        {
            double f0 = FMin + c * Band / Cols, f1 = f0 + Band / Cols;
            int b0 = Math.Max(1, (int)Math.Round(f0 / binHz)), b1 = Math.Min(n / 2 - 1, Math.Max(b0, (int)Math.Round(f1 / binHz)));
            double m = 1e-20;
            for (int b = b0; b <= b1; b++) m = Math.Max(m, _re[b] * _re[b] + _im[b] * _im[b]);
            _row[c] = 10 * Math.Log10(m);
        }
        // noise floor of the line: its median, smoothed over a few seconds
        var sorted = (double[])_row.Clone();
        Array.Sort(sorted);
        double med = sorted[Cols / 2];
        _floor = double.IsNaN(_floor) ? med : _floor + 0.1 * (med - _floor);

        double range = 40 - Contrast * 0.3;          // 40 dB (contrast 0) to 10 dB (100)
        double offset = (Gain - 50) / 50 * 0.35;
        using (var fb = _bmp.Lock())
        unsafe
        {
            uint* p = (uint*)fb.Address;
            int stride = fb.RowBytes / 4;
            Buffer.MemoryCopy(p, p + stride, (long)stride * 4 * (Cap - 1), (long)stride * 4 * (Cap - 1));
            for (int c = 0; c < Cols; c++)
                p[c] = Palette((_row[c] - _floor) / range + 0.12 + offset);
        }
        Array.Copy(_times, 0, _times, 1, Cap - 1);
        _times[0] = DateTime.UtcNow;
        InvalidateVisual();
    }

    static readonly IBrush LiveFill = new SolidColorBrush(Color.Parse("#10232B"), 0.92);
    static readonly IBrush LiveText = new SolidColorBrush(Color.Parse("#8ADBEC"));
    static readonly Pen LivePen = new(new SolidColorBrush(Color.Parse("#5CC8E0")), 1, new DashStyle([3, 2], 0));
    // (a PI4 frame: the green of its zone)
    static readonly IBrush LivePi4Fill = new SolidColorBrush(Color.Parse("#16260F"), 0.92);
    static readonly IBrush LivePi4Text = new SolidColorBrush(Color.Parse("#B5E39A"));
    static readonly Pen LivePi4Pen = new(new SolidColorBrush(Color.Parse("#8FD16A")), 1, new DashStyle([3, 2], 0));

    // a signal being received: a dashed label at the line where it was
    // first seen, going down with the picture (testers: a band at the
    // top took room from the waterfall)
    void InProgressMark(DrawingContext ctx, WaterfallMark m, double x, double y, double w)
    {
        var text = new FormattedText(m.Label, System.Globalization.CultureInfo.CurrentCulture, FlowDirection.LeftToRight, Face, 11, m.Pi4 ? LivePi4Text : LiveText);
        var box = new Rect(Math.Clamp(x - text.Width / 2 - 6, 0, Math.Max(0, w - text.Width - 12)), y, text.Width + 12, text.Height + 4);
        ctx.FillRectangle(m.Pi4 ? LivePi4Fill : LiveFill, box, 3);
        ctx.DrawRectangle(m.Pi4 ? LivePi4Pen : LivePen, box, 3);
        ctx.DrawText(text, new Point(box.X + 6, box.Y + 2));
    }

    // the line drawn at `t` (the first one not newer than it), -1 if `t`
    // is older than every line shown
    // the time of a line shown (0: the newest), for the screenshots
    public DateTime? TimeOfRow(int r) => r >= 0 && r < Rows && _times[r] != DateTime.MinValue ? _times[r] : null;

    int RowOf(DateTime t)
    {
        for (int r = 0; r < Rows && _times[r] != DateTime.MinValue; r++)
            if (_times[r] <= t) return r;
        return -1;
    }

    // dark blue -> blue -> cyan -> yellow -> white
    static uint Palette(double v)
    {
        v = Math.Clamp(v, 0, 1);
        double r = Math.Clamp(3 * v - 1.6, 0, 1), g = Math.Clamp(2.2 * v - 0.5, 0, 1), b = Math.Clamp(0.12 + 1.6 * v, 0, 1) * (1 - 0.5 * v);
        r = r * 0.9 + 0.02; g = g * 0.85 + 0.05; b = b * 0.8 + 0.1;
        return 0xFF000000u | ((uint)(r * 255) << 16) | ((uint)(g * 255) << 8) | (uint)(b * 255);
    }

    static readonly IBrush ZoneFill = new SolidColorBrush(Color.Parse("#5CC8E0"), 0.06);
    static readonly IBrush ZoneLine = new SolidColorBrush(Color.Parse("#5CC8E0"));
    static readonly IBrush Pi4ZoneFill = new SolidColorBrush(Color.Parse("#8FD16A"), 0.06);
    static readonly IBrush Pi4ZoneLine = new SolidColorBrush(Color.Parse("#8FD16A"));
    static readonly IBrush AxisBg = new SolidColorBrush(Color.Parse("#10161D"));
    static readonly IBrush AxisText = new SolidColorBrush(Color.Parse("#93A1AF"));
    static readonly IBrush TickBrush = new SolidColorBrush(Color.Parse("#4A5866"));
    static readonly Typeface Face = new("Inter");

    // the minute line: bright dashes over a dark line, and the time on a
    // dark box (testers: hardly seen in the noise)
    static readonly IBrush MinuteText = new SolidColorBrush(Color.Parse("#E6EDF3"));
    static readonly IBrush MinuteBox = new SolidColorBrush(Color.Parse("#05090F"), 0.8);
    static readonly Pen MinuteUnder = new(new SolidColorBrush(Color.Parse("#000000"), 0.55), 3);
    static readonly Pen MinutePen = new(new SolidColorBrush(Color.Parse("#E6EDF3"), 0.85), 1, new DashStyle([4, 3], 0));

    public override void Render(DrawingContext ctx)
    {
        // the axis on top, where the lines come in; the picture under it
        double w = Bounds.Width, top = AxisHeight, h = Math.Max(0, Bounds.Height - AxisHeight);
        if (w < 40 || h < 1) return;                      // hidden or not laid out yet
        SameAxis();
        double X(double f) => (f - FMin) / Band * w;
        double Y(int row) => top + (double)row / Rows * h;

        // the picture, the zone, the labels and the axis
        ctx.DrawImage(_bmp, new Rect(0, 0, Cols, Rows), new Rect(0, top, w, h));

        // a thin dashed line where a new minute (every MinuteLines) begins,
        // its UTC time at the left edge
        int every = MinuteLines;
        if (every > 0)
            for (int r = 1; r < Rows && _times[r] != DateTime.MinValue; r++)
            {
                long a = (long)Math.Floor(_times[r - 1].Ticks / (double)TimeSpan.TicksPerMinute / every);
                long b = (long)Math.Floor(_times[r].Ticks / (double)TimeSpan.TicksPerMinute / every);
                if (a == b) continue;
                double y = Y(r - 1) + 0.5;
                ctx.DrawLine(MinuteUnder, new Point(0, y), new Point(w, y));
                ctx.DrawLine(MinutePen, new Point(0, y), new Point(w, y));
                var t = new FormattedText(new DateTime(a * every * TimeSpan.TicksPerMinute).ToString("HH:mm"),
                    System.Globalization.CultureInfo.InvariantCulture, FlowDirection.LeftToRight, Face, 11, MinuteText);
                // under the line, or above it at the bottom of the picture
                double ty = y + t.Height + 4 < top + h ? y + 2 : y - t.Height - 4;
                if (ty < top) continue;
                ctx.FillRectangle(MinuteBox, new Rect(2, ty, t.Width + 8, t.Height + 2), 2);
                ctx.DrawText(t, new Point(6, ty + 1));
            }

        // search zones: the OOK frames' (cyan), PI4's (green), dashed
        void Zone(double c, double hw, IBrush fill, IBrush line)
        {
            double z0 = X(c - hw), z1 = X(c + hw);
            ctx.FillRectangle(fill, new Rect(z0, top, z1 - z0, h));
            var dashed = new Pen(line, 1.5, new DashStyle([4, 4], 0));
            ctx.DrawLine(dashed, new Point(z0, top), new Point(z0, top + h));
            ctx.DrawLine(dashed, new Point(z1, top), new Point(z1, top + h));
        }
        if (ShowZone) Zone(Center, HalfWidth, ZoneFill, ZoneLine);
        if (ShowPi4Zone) Zone(Pi4Center, Pi4HalfWidth, Pi4ZoneFill, Pi4ZoneLine);
        // (no line at the centre: it was taken for a carrier)

        // decoded frames, at the line of the moment they ended; signals
        // being received, at the line where they were first seen
        if (Marks is not null && _times[0] != DateTime.MinValue)
            foreach (var o in Marks)
            {
                if (o is not WaterfallMark m || m.FreqHz < FMin || m.FreqHz > FMax) continue;
                int r = RowOf(m.TimeUtc);
                if (r < 0) continue;
                double y = Y(r);
                if (y > top + h - 16) continue;
                if (m.InProgress) { InProgressMark(ctx, m, X(m.FreqHz), y, w); continue; }
                var text = new FormattedText(m.Label, System.Globalization.CultureInfo.CurrentCulture, FlowDirection.LeftToRight,
                    Face, 11, new SolidColorBrush(Color.Parse(m.Classic ? "#8ADBEC" : "#F2C46B")));
                var box = new Rect(Math.Clamp(X(m.FreqHz) - text.Width / 2 - 6, 0, Math.Max(0, w - text.Width - 12)), y,
                                   text.Width + 12, text.Height + 4);
                ctx.FillRectangle(new SolidColorBrush(Color.Parse(m.Classic ? "#10232B" : "#2A2412"), 0.92), box, 3);
                ctx.DrawRectangle(new Pen(new SolidColorBrush(Color.Parse(m.Classic ? "#3E8FA3" : "#6B5420"))), box, 3);
                ctx.DrawText(text, new Point(box.X + 6, box.Y + 2));
            }

        // frequency axis, on top: a tick every 50 Hz (100 if too dense)
        // pointing at the picture, a value every 100 Hz (200 or 500 if narrow)
        ctx.FillRectangle(AxisBg, new Rect(0, 0, w, AxisHeight));
        double pxHz = w / Band;
        double tickStep = pxHz * 50 >= 8 ? 50 : 100;
        double labelStep = pxHz * 100 >= 46 ? 100 : pxHz * 200 >= 46 ? 200 : 500;
        for (double f = Math.Ceiling(FMin / tickStep) * tickStep; f <= FMax; f += tickStep)
        {
            double x = X(f);
            bool major = Math.Abs(f % labelStep) < 1e-6;
            ctx.DrawLine(new Pen(TickBrush, 1), new Point(x, AxisHeight - (major ? 6 : 3)), new Point(x, AxisHeight));
            if (!major) continue;
            var t = new FormattedText(f.ToString("0"), System.Globalization.CultureInfo.InvariantCulture, FlowDirection.LeftToRight,
                Face, 11, AxisText);
            ctx.DrawText(t, new Point(Math.Clamp(x - t.Width / 2, 0, Math.Max(0, w - t.Width)), 1));
        }
    }
}
