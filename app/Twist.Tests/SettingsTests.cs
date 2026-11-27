using System.Text;
using Twist.App.Services;
using Twist.App.ViewModels;
using Twist.Native;
using Xunit;

namespace Twist.Tests;

public class SettingsTests
{
    static string Temp(string name)
    {
        var dir = Path.Combine(Path.GetTempPath(), "twist-tests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        return Path.Combine(dir, name);
    }

    [Fact]
    public void Missing_file_gives_defaults()
    {
        var s = Settings.Load(Temp("none.json"), out var problem);
        Assert.Null(problem);
        Assert.Equal(1500, s.Current.Decoding.CenterHz);
        Assert.Single(s.Profiles);
    }

    [Fact]
    public void Round_trip_keeps_every_value()
    {
        var path = Temp("twist.json");
        var s = new Settings();
        s.Station.Callsign = "F6FBB";
        s.Station.Locator = "JN12AX";
        s.Current.Decoding.ChainDelaySeconds = 1;
        s.Current.Decoding.TwistS = false;
        s.Current.Audio.Input = "CABLE Output (VB-Audio Virtual Cable) (Windows WASAPI)";
        s.Interface.Waterfall.FullBand = true;
        s.Interface.Waterfall.Band = "200-2000";
        s.Interface.Waterfall.MinuteLines = 5;
        s.Interface.Waterfall.LiveFrequency = true;
        s.Interface.SpotsWindow.Detached = true;
        s.Interface.SpotsWindow.Window.X = 1920;              // on the second screen
        s.Reports.LogFolder = "C:\\Radio\\Journal été";
        s.Save(path);

        var r = Settings.Load(path, out var problem);
        Assert.Null(problem);
        Assert.Equal("F6FBB", r.Station.Callsign);
        Assert.Equal(1, r.Current.Decoding.ChainDelaySeconds);
        Assert.False(r.Current.Decoding.TwistS);
        Assert.Equal(s.Current.Audio.Input, r.Current.Audio.Input);
        Assert.True(r.Interface.Waterfall.FullBand);
        Assert.Equal("200-2000", r.Interface.Waterfall.Band);
        Assert.Equal(5, r.Interface.Waterfall.MinuteLines);
        Assert.True(r.Interface.Waterfall.LiveFrequency);
        Assert.True(r.Interface.SpotsWindow.Detached);
        Assert.Equal(1920, r.Interface.SpotsWindow.Window.X);
        Assert.False(r.Interface.WaterfallWindow.Detached);
        Assert.Equal("C:\\Radio\\Journal été", r.Reports.LogFolder);
        Assert.Contains("Journal été", File.ReadAllText(path));      // readable: accents not escaped
        Assert.False(File.Exists(path + ".tmp"));
    }

    [Fact]
    public void Hand_edited_file_partial_and_unknown_fields()
    {
        var path = Temp("twist.json");
        File.WriteAllText(path, """
            // a comment
            { "station": { "callsign": "F1HDI" }, "future": 3, "profiles": [ { "name": "2 m", "decoding": null } ], }
            """);
        var s = Settings.Load(path, out var problem);
        Assert.Null(problem);
        Assert.Equal("F1HDI", s.Station.Callsign);
        Assert.Equal("2 m", s.Current.Name);
        Assert.Equal(1500, s.Current.Decoding.CenterHz);           // missing part: defaults
        Assert.False(s.Interface.JournalWindow.Detached);           // (a file of 0.12: no detached parts)
        Assert.Equal(1100, s.Interface.WaterfallWindow.Window.Width);
    }

    [Fact]
    public void Damaged_file_kept_aside_and_defaults_used()
    {
        var path = Temp("twist.json");
        File.WriteAllText(path, "{ \"station\": ");
        var s = Settings.Load(path, out var problem);
        Assert.NotNull(problem);
        Assert.True(File.Exists(path + ".bad"));
        Assert.Equal("", s.Station.Callsign);
    }

    [Fact]
    public void Csv_one_file_per_utc_day_with_header_once()
    {
        var dir = Path.GetDirectoryName(Temp("x"))!;
        var log = new CsvLog(dir);
        var t = new DateTime(2026, 10, 28, 14, 14, 0, DateTimeKind.Utc);
        var a = new Spot(t, TwlProto.TwistL, "F5ZZO", "JN03TJ", -12.4, 1549.2, false);
        var b = a with { StartUtc = t.AddMinutes(15), Snr = -20.6, Deep = true, Loc = "" };
        var c = a with { StartUtc = t.AddDays(1) };
        var p1 = log.Append(a, "TWIST L", 144.490549, 204.3, 178.2, "CABLE; Output");
        log.Append(b, "TWIST L", null, null, null, "x", "préfixe inconnu");
        var p2 = log.Append(c, "TWIST L", 144.490549, null, null, "x");

        Assert.EndsWith("twist-2026-10-28.csv", p1);
        Assert.EndsWith("twist-2026-10-29.csv", p2);
        var bytes = File.ReadAllBytes(p1);
        Assert.Equal(new byte[] { 0xEF, 0xBB, 0xBF }, bytes[..3]);   // BOM, for Excel
        var lines = File.ReadAllLines(p1, Encoding.UTF8);
        Assert.Equal(3, lines.Length);
        Assert.Equal(CsvLog.Header, lines[0]);
        Assert.Equal("2026-10-28;14:14:00;TWIST L;F5ZZO;JN03TJ;-12;1549;144.490549;204;178;0;\"CABLE; Output\";", lines[1]);
        Assert.Equal("2026-10-28;14:29:00;TWIST L;F5ZZO;;-21;1549;;;;1;x;préfixe inconnu", lines[2]);
    }

    [Fact]
    public void Dialog_checks_fields_and_builds_settings()
    {
        var cfg = new Settings();
        cfg.Current.Audio.Input = "Old card (MME)";
        var vm = new SettingsViewModel(cfg, "x.json", ["Card A (WASAPI)"], []);
        Assert.Contains("Old card (MME) — absent", vm.InputDevices);   // a saved device stays listed
        Assert.Equal("Old card (MME) — absent", vm.InputDevice);

        vm.Callsign = "f6fbb"; vm.Locator = "JN12X";                     // wrong locator
        Assert.Null(vm.TryBuild());
        Assert.Equal(0, vm.Tab);
        Assert.Contains("Locator", vm.Error);

        vm.Locator = "jn12ax"; vm.ChainDelay = "1,2"; vm.PassPeriod = "40";
        Assert.Null(vm.TryBuild());
        Assert.Equal(3, vm.Tab);
        Assert.Contains("Période", vm.Error);

        vm.PassPeriod = "3"; vm.InputDevice = "Card A (WASAPI)"; vm.KnownCalls = "f5zzo f1hdi,F5ZZO";
        var r = vm.TryBuild();
        Assert.NotNull(r);
        Assert.Equal("", vm.Error);
        Assert.Equal("F6FBB", r!.Station.Callsign);
        Assert.Equal("JN12AX", r.Station.Locator);
        Assert.Equal("F5ZZO, F1HDI", r.Station.KnownCalls);
        Assert.Equal(1.2, r.Current.Decoding.ChainDelaySeconds, 6);
        Assert.Equal("Card A (WASAPI)", r.Current.Audio.Input);
        Assert.Equal("Old card (MME)", cfg.Current.Audio.Input);          // the original is untouched
    }

    [Fact]
    public void Profiles_created_renamed_switched_and_deleted()
    {
        var cfg = new Settings();
        cfg.Current.Name = "2 m";
        cfg.Current.Decoding.CenterHz = 1500;
        var vm = new SettingsViewModel(cfg, "x.json", [], []);
        Assert.Equal(["2 m"], vm.Profiles);
        Assert.False(vm.CanDeleteProfile);

        vm.CenterHz = "1450";
        Assert.Contains("existe déjà", vm.NewProfile("2 m", true));
        Assert.Contains("nom", vm.NewProfile("  ", true));
        Assert.Null(vm.NewProfile("70 cm", copyCurrent: true));     // a copy, now shown
        Assert.Equal(["2 m", "70 cm"], vm.Profiles);
        Assert.Equal("70 cm", vm.Profile);
        Assert.Equal("1450", vm.CenterHz);                         // with the edited value
        vm.CenterHz = "1700";
        Assert.Null(vm.NewProfile("vierge", copyCurrent: false));   // the defaults
        Assert.Equal("1500", vm.CenterHz);
        vm.DeleteProfile();
        Assert.Equal(["2 m", "70 cm"], vm.Profiles);

        vm.Profile = "2 m";                                         // switching stores the fields first
        Assert.Equal("1450", vm.CenterHz);
        vm.CenterHz = "99999";                                      // invalid: no switch
        vm.Profile = "70 cm";
        Assert.Contains("Centre", vm.Error);
        Assert.Equal("2 m", vm.Profile);
        vm.CenterHz = "1450";
        vm.Profile = "70 cm";
        Assert.Equal("1700", vm.CenterHz);

        Assert.Contains("existe déjà", vm.RenameProfile("2 m"));
        Assert.Null(vm.RenameProfile("70 cm IC-9700"));
        Assert.Equal(["2 m", "70 cm IC-9700"], vm.Profiles);
        var r = vm.TryBuild()!;
        Assert.Equal("70 cm IC-9700", r.Profile);
        Assert.Equal(1700, r.Current.Decoding.CenterHz);
        Assert.Equal(1450, r.Profiles.First(x => x.Name == "2 m").Decoding.CenterHz);

        vm.DeleteProfile();
        Assert.Equal(["2 m"], vm.Profiles);
        Assert.Single(vm.TryBuild()!.Profiles);
        Assert.Equal("2 m", cfg.Current.Name);                      // the original untouched
        Assert.Single(cfg.Profiles);
    }
}
