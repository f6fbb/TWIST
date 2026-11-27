using Avalonia.Threading;
using Twist.App.Lang;
using Twist.App.Services;

namespace Twist.App.ViewModels;

// The locator of a spot received without one (OPERA 05, TWIST S): from
// the list of the settings, the TWIST L frames already received, or
// fbbspots (Services/Locators.cs); shown greyed, in lower case, in the list of spots,
// with where it comes from, and the distance and azimuth from it. Only
// shown: the CSV journal and MQTT keep what the frame carried.
public partial class MainWindowViewModel
{
    readonly LocatorBook _book;

    void FindLocator(SpotViewModel spot, double audioHz)
    {
        if (_book.Find(spot.Call) is { } f) { ShowLocator(spot, f); return; }
        if (!_cfg.Station.LocatorLookup) return;
        int band = Num(RadioFrequency) is double mhz && mhz > 0 ? (int)(mhz + audioHz / 1e6) : 0;
        var call = spot.Call;
        _ = Task.Run(async () =>
        {
            if (await _book.LookupAsync(call, band) is { } r)
                Dispatcher.UIThread.Post(() =>
                {
                    foreach (var sp in Spots.Where(x => x.Call == call && x.Loc == "—")) ShowLocator(sp, r);
                });
        });
    }

    void ShowLocator(SpotViewModel spot, LocatorFound f)
    {
        spot.Loc = f.Locator;
        spot.LocGuessed = true;
        spot.LocTip = f.Source switch
        {
            LocSource.List => L.T("Locator de la liste des Réglages (Station), pas reçu dans la trame"),
            LocSource.Heard => L.F("Locator reçu dans une trame TWIST L le {0}, pas dans celle-ci", f.Utc.ToString("dd/MM/yyyy HH:mm 'UTC'", L.Culture)),
            _ => f.Approximate ? L.T("Locator trouvé sur fbbspots (spots.f6fbb.org), celui de l'indicatif sans suffixe")
                               : L.T("Locator trouvé sur fbbspots (spots.f6fbb.org), pas reçu dans la trame"),
        };
        if (Locator != "" && Maidenhead.DistanceAzimuth(Locator, f.Locator) is { } v)
        {
            spot.Distance = $"{v.km:0} km";
            spot.Azimuth = $"{v.az:0}°";
        }
    }
}
