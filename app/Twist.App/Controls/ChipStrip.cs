using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace Twist.App.Controls;

// The chips of a frame as a strip of bars: high for 1 (carrier on), low
// for 0. With Sent >= 0 (transmission in progress), the chips already
// sent are bright and the others dim.
public class ChipStrip : Control
{
    public static readonly StyledProperty<byte[]?> ChipsProperty =
        AvaloniaProperty.Register<ChipStrip, byte[]?>(nameof(Chips));
    public static readonly StyledProperty<int> SentProperty =
        AvaloniaProperty.Register<ChipStrip, int>(nameof(Sent), -1);
    public static readonly StyledProperty<IBrush> OnBrushProperty =
        AvaloniaProperty.Register<ChipStrip, IBrush>(nameof(OnBrush), new SolidColorBrush(Color.Parse("#F2C46B")));
    public static readonly StyledProperty<IBrush> DimBrushProperty =
        AvaloniaProperty.Register<ChipStrip, IBrush>(nameof(DimBrush), new SolidColorBrush(Color.Parse("#3A4652")));

    public byte[]? Chips { get => GetValue(ChipsProperty); set => SetValue(ChipsProperty, value); }
    public int Sent { get => GetValue(SentProperty); set => SetValue(SentProperty, value); }
    public IBrush OnBrush { get => GetValue(OnBrushProperty); set => SetValue(OnBrushProperty, value); }
    public IBrush DimBrush { get => GetValue(DimBrushProperty); set => SetValue(DimBrushProperty, value); }

    static ChipStrip() => AffectsRender<ChipStrip>(ChipsProperty, SentProperty, OnBrushProperty, DimBrushProperty);

    public override void Render(DrawingContext ctx)
    {
        var c = Chips;
        if (c is null || c.Length == 0) return;
        double w = Bounds.Width / c.Length, h = Bounds.Height;
        for (int i = 0; i < c.Length; i++)
        {
            bool sent = Sent < 0 || i < Sent;
            double bh = c[i] != 0 ? h : h * 0.12;
            var brush = sent ? (c[i] != 0 ? OnBrush : DimBrush) : DimBrush;
            ctx.FillRectangle(brush, new Rect(i * w, h - bh, Math.Max(1, w - 0.6), bh));
        }
    }
}
