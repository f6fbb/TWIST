using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Twist.App.Lang;

namespace Twist.App.Controls;

// Input level bar graph, -60 to 0 dBFS. The zones stay visible in dark
// colours behind the bar; the bar takes the colour of its zone:
// too low < -40 dBFS, good -40 to -10, high -10 to -3, saturation > -3.
// A white mark shows the recent peak.
public class LevelMeter : Control
{
    public static readonly StyledProperty<double> LevelProperty =
        AvaloniaProperty.Register<LevelMeter, double>(nameof(Level), -120);
    public static readonly StyledProperty<double> PeakProperty =
        AvaloniaProperty.Register<LevelMeter, double>(nameof(Peak), -120);

    public double Level { get => GetValue(LevelProperty); set => SetValue(LevelProperty, value); }
    public double Peak { get => GetValue(PeakProperty); set => SetValue(PeakProperty, value); }

    static LevelMeter() => AffectsRender<LevelMeter>(LevelProperty, PeakProperty);

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == ThemeVariantScope.ActualThemeVariantProperty) InvalidateVisual();   // theme changed
    }

    public const double Low = -40, High = -10, Sat = -3;

    // zone background (dark theme, light theme) and lit colour
    static readonly (double from, double to, Color dark, Color pale, Color lit)[] Zones =
    [
        (-60, Low, Color.Parse("#22303F"), Color.Parse("#DCE4EC"), Color.Parse("#6F8FB0")),
        (Low, High, Color.Parse("#18301F"), Color.Parse("#D6EEDD"), Color.Parse("#4FBF7A")),
        (High, Sat, Color.Parse("#3A2C12"), Color.Parse("#F7E6C4"), Color.Parse("#F0A030")),
        (Sat, 0, Color.Parse("#3D1A1A"), Color.Parse("#F6D3D0"), Color.Parse("#FF5A52")),
    ];

    public static Color ZoneColor(double db)
    {
        foreach (var z in Zones) if (db < z.to) return z.lit;
        return Zones[^1].lit;
    }

    public static string ZoneName(double db) =>
        db < Low ? L.T("trop bas") : db < High ? L.T("bon") : db < Sat ? L.T("élevé") : L.T("saturation");

    static double X(double db, double w) => Math.Clamp((db + 60) / 60, 0, 1) * w;

    public override void Render(DrawingContext ctx)
    {
        double w = Bounds.Width, h = Bounds.Height;
        bool light = ActualThemeVariant == Avalonia.Styling.ThemeVariant.Light;
        foreach (var z in Zones)
            ctx.FillRectangle(new SolidColorBrush(light ? z.pale : z.dark), new Rect(X(z.from, w), 0, X(z.to, w) - X(z.from, w), h));
        ctx.FillRectangle(new SolidColorBrush(ZoneColor(Level)), new Rect(0, 0, X(Level, w), h));
        if (Peak > -60)
            ctx.FillRectangle(light ? Avalonia.Media.Brushes.Black : Avalonia.Media.Brushes.WhiteSmoke, new Rect(Math.Max(0, X(Peak, w) - 2), 0, 2, h));
    }
}
