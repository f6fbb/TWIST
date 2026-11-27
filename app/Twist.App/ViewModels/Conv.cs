using System.Globalization;
using Avalonia.Data;
using Avalonia.Data.Converters;

namespace Twist.App.ViewModels;

public static class Conv
{
    // RadioButton group bound to an int: checked when value == parameter
    public static readonly IValueConverter IntEquals = new IntEqualsConverter();

    sealed class IntEqualsConverter : IValueConverter
    {
        public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture) =>
            value is int v && int.TryParse(parameter?.ToString(), out var p) && v == p;

        public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
            value is true && int.TryParse(parameter?.ToString(), out var p) ? p : BindingOperations.DoNothing;
    }
}
