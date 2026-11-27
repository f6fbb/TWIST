namespace Twist.App.Services;

// Maidenhead locators: centre of a square, distance and azimuth on the
// sphere (great circle), for the spots table.
public static class Maidenhead
{
    public static (double lat, double lon)? Center(string loc)
    {
        loc = loc.Trim().ToUpperInvariant();
        if (loc.Length != 4 && loc.Length != 6) return null;
        if (loc[0] < 'A' || loc[0] > 'R' || loc[1] < 'A' || loc[1] > 'R' ||
            !char.IsDigit(loc[2]) || !char.IsDigit(loc[3])) return null;
        double lon = (loc[0] - 'A') * 20 - 180 + (loc[2] - '0') * 2;
        double lat = (loc[1] - 'A') * 10 - 90 + (loc[3] - '0');
        if (loc.Length == 6)
        {
            if (loc[4] < 'A' || loc[4] > 'X' || loc[5] < 'A' || loc[5] > 'X') return null;
            lon += (loc[4] - 'A') * (2.0 / 24) + 1.0 / 24;
            lat += (loc[5] - 'A') * (1.0 / 24) + 0.5 / 24;
        }
        else { lon += 1; lat += 0.5; }
        return (lat, lon);
    }

    // km and degrees from `from` to `to`; null if a locator is invalid.
    public static (double km, double az)? DistanceAzimuth(string from, string to)
    {
        if (Center(from) is not { } a || Center(to) is not { } b) return null;
        double R = 6371.0, d2r = Math.PI / 180;
        double la1 = a.lat * d2r, la2 = b.lat * d2r, dl = (b.lon - a.lon) * d2r;
        double c = Math.Sin(la1) * Math.Sin(la2) + Math.Cos(la1) * Math.Cos(la2) * Math.Cos(dl);
        double km = R * Math.Acos(Math.Clamp(c, -1, 1));
        double az = Math.Atan2(Math.Sin(dl) * Math.Cos(la2), Math.Cos(la1) * Math.Sin(la2) - Math.Sin(la1) * Math.Cos(la2) * Math.Cos(dl)) / d2r;
        return (km, (az + 360) % 360);
    }
}
