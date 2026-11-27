using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace Twist.App.Controls;

// The processor used by the program, 0 to 100 %, under the input level
// and drawn the same way: right < 60 %, high 60 to 85 %, too high above
// (the passes may then come late).
public class CpuMeter : Control
{
    public static readonly StyledProperty<double> PercentProperty =
        AvaloniaProperty.Register<CpuMeter, double>(nameof(Percent));
    public double Percent { get => GetValue(PercentProperty); set => SetValue(PercentProperty, value); }

    static CpuMeter() => AffectsRender<CpuMeter>(PercentProperty);

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == ThemeVariantScope.ActualThemeVariantProperty) InvalidateVisual();
    }

    public const double High = 60, TooHigh = 85;

    static readonly (double from, double to, Color dark, Color pale, Color lit)[] Zones =
    [
        (0, High, Color.Parse("#18301F"), Color.Parse("#D6EEDD"), Color.Parse("#4FBF7A")),
        (High, TooHigh, Color.Parse("#3A2C12"), Color.Parse("#F7E6C4"), Color.Parse("#F0A030")),
        (TooHigh, 100, Color.Parse("#3D1A1A"), Color.Parse("#F6D3D0"), Color.Parse("#FF5A52")),
    ];

    static double X(double p, double w) => Math.Clamp(p / 100, 0, 1) * w;

    public override void Render(DrawingContext ctx)
    {
        double w = Bounds.Width, h = Bounds.Height;
        bool light = ActualThemeVariant == Avalonia.Styling.ThemeVariant.Light;
        foreach (var z in Zones)
            ctx.FillRectangle(new SolidColorBrush(light ? z.pale : z.dark), new Rect(X(z.from, w), 0, X(z.to, w) - X(z.from, w), h));
        var lit = Percent < High ? Zones[0].lit : Percent < TooHigh ? Zones[1].lit : Zones[2].lit;
        ctx.FillRectangle(new SolidColorBrush(lit), new Rect(0, 0, X(Math.Max(Percent, 1), w), h));
    }
}
