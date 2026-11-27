using System.Diagnostics;
using System.Text;
using MQTTnet;
using MQTTnet.Client;
using Twist.App.Services;
using Twist.Native;
using Xunit;

namespace Twist.Tests;

public class RadioTests
{
    [Fact]
    public void Model_list_parsed()
    {
        var text = " Rig #  Mfg                    Model                   Version         Status      Macro\n" +
                   "     1  Hamlib                 Dummy                   20221128.0      Stable      RIG_MODEL_DUMMY\n" +
                   "  1035  Yaesu                  FT-991                  20230328.15     Stable      RIG_MODEL_FT991\n" +
                   "     5  TRXManager             TRXManager 5.7.630+     20210613.0      Stable      RIG_MODEL_TRXMANAGER_RIG\r\n";
        var m = Hamlib.ParseModels(text);
        Assert.Equal(3, m.Count);
        Assert.Equal(new Hamlib.Model(1035, "Yaesu", "FT-991"), m[1]);
        Assert.Equal("TRXManager 5.7.630+", m[2].Name);
        Assert.Equal("Yaesu FT-991 (1035)", m[1].ToString());
    }

    [Fact]
    public void Rigctld_arguments()
    {
        var r = new RadioSettings { Cat = "hamlib", RigModel = 1035, Port = "COM4", Baud = 38400, Ptt = "CAT" };
        Assert.Equal("-m 1035 -r COM4 -s 38400 -t 4534 -T 127.0.0.1 -P RIG", Rig.DaemonArgs(r, 4534));
        r.Ptt = "RTS"; r.PttPort = "COM4";                   // RTS of the CAT port: rigctld's
        Assert.EndsWith("-P RTS", Rig.DaemonArgs(r, 4534));
        r.PttPort = "COM7";                                 // another port: opened by TWIST
        Assert.EndsWith("-T 127.0.0.1", Rig.DaemonArgs(r, 4534));
    }

    // a real rigctld (Hamlib's dummy radio), when Hamlib is installed
    [Fact]
    public void Dummy_radio_frequency_and_ptt()
    {
        if (Hamlib.Tool("rigctld") is null) return;
        Assert.Contains(Hamlib.Models(), x => x.Number == 1);
        var s = new RadioSettings { Cat = "hamlib", RigModel = 1, Port = "", Baud = 0, Ptt = "CAT", DataPtt = true };
        var msgs = new List<string>();
        using var rig = new Rig(s);
        rig.Message += (m, _) => { lock (msgs) msgs.Add(m); };
        rig.Open();
        Assert.True(rig.Connected);
        Assert.True(rig.FrequencyHz > 1e6);
        Assert.True(rig.SetPtt(true));
        Assert.True(rig.PttOn);
        Assert.True(rig.SetPtt(false));
        Assert.False(rig.PttOn);
    }

    [Fact]
    public void Running_rigctld_on_the_network()
    {
        if (Hamlib.Tool("rigctld") is not string exe) return;
        using var d = Process.Start(new ProcessStartInfo(exe, "-m 1 -t 4597 -P RIG") { UseShellExecute = false, RedirectStandardError = true })!;
        try
        {
            using var rig = new Rig(new RadioSettings { Cat = "net", NetHost = "127.0.0.1", NetPort = 4597, Ptt = "CAT", DataPtt = false });
            rig.Open();
            Assert.True(rig.Connected);
            Assert.True(rig.SetPtt(true));
            Assert.True(rig.SetPtt(false));
        }
        finally { d.Kill(); }
    }

    [Fact]
    public void No_radio_reports_why()
    {
        using var rig = new Rig(new RadioSettings { Cat = "net", NetHost = "127.0.0.1", NetPort = 4596, Ptt = "CAT" });
        var e = Assert.Throws<InvalidOperationException>(rig.Open);
        Assert.Contains("4596", e.Message);
    }

    [Fact]
    public void Mqtt_message_as_spyfbbclient()
    {
        var s = new Spot(new DateTime(2026, 10, 28, 13, 47, 0, DateTimeKind.Utc), TwlProto.TwistL, "F5ZZO", "JN12AX", -23.6, 1503.2, false);
        Assert.Equal("{\"date\":\"2026-10-28T13:47:00Z\",\"mode\":\"TWIST\",\"submode\":\"L\",\"frequency\":\"144492503.2\",\"snr\":\"-24\"," +
                     "\"payload\":\"F5ZZO JN12AX\",\"callsign\":\"F5ZZO\",\"locator\":\"JN12AX\",\"deep\":\"0\",\"reporter\":\"F1HDI/P\",\"rgrid\":\"IN96HA\"," +
                     $"\"software\":\"TWIST Console {Report.Version}\"}}",
                     Mqtt.Json(s, "TWIST L", 144491000 + 1503.2, "F1HDI/P", "IN96HA"));
        var c = s with { Proto = TwlProto.Classic, Call = "F5ZRV", Loc = "", Deep = true };
        var j = Mqtt.Json(c, LibTwist.ProtoName(TwlProto.Classic), 144488004.9, "F1HDI/P", "IN96HA");
        Assert.Contains("\"submode\":\"05\"", j);
        Assert.Contains("\"payload\":\"F5ZRV\",\"callsign\":\"F5ZRV\",\"deep\":\"1\"", j);
        Assert.DoesNotContain("locator", j);
    }

    // the program in each spot (fbbspots: spots.software) and the
    // User-Agent: the version of the assembly, without the SDK's "+hash"
    [Fact]
    public void Software_of_the_spots()
    {
        Assert.Equal("0.27.1", Report.Clean("0.27.1+3f2a9c0d1e2b"));
        Assert.Equal("1.0.4", Report.Clean(" 1.0.4 "));
        Assert.Equal("1.0.0-rc.1", Report.Clean("1.0.0-rc.1+3f2a9c0d1e2b"));
        var v = typeof(Report).Assembly.GetName().Version!;
        Assert.Equal($"{v.Major}.{v.Minor}.{v.Build}", Report.Version.Split('-')[0]);
        // one version for the program and its libraries (twdefs.h, Twist.App.csproj)
        if (Twist.App.ViewModels.Engine.Loaded) Assert.Equal(Report.Version, Twist.Native.LibTwist.Version);
        Assert.Equal("TWIST Console " + Report.Version, Report.Software);
        Assert.True(Report.Software.Length <= 48 && Report.Software.All(c => c >= ' ' && c < 127));
        Assert.Equal("TWIST-Console/" + Report.Version, Fbbspots.UserAgent);
    }

    // through a broker on this computer (mosquitto), TWIST_MQTT=1
    [Fact]
    public void Mqtt_published_and_received()
    {
        if (Environment.GetEnvironmentVariable("TWIST_MQTT") != "1") return;
        var got = new List<(string, string)>();
        using var sub = new MqttFactory().CreateMqttClient();
        sub.ApplicationMessageReceivedAsync += e =>
        {
            lock (got) got.Add((e.ApplicationMessage.Topic, Encoding.UTF8.GetString(e.ApplicationMessage.PayloadSegment)));
            return Task.CompletedTask;
        };
        sub.ConnectAsync(new MqttClientOptionsBuilder().WithTcpServer("127.0.0.1", 1883).Build()).Wait();
        sub.SubscribeAsync("spyfbbclient/+/spot").Wait();

        var rs = new ReportSettings { Mqtt = true, MqttHost = "127.0.0.1", MqttPort = 1883, MqttTopic = "spyfbbclient/TEST/spot" };
        Assert.Contains("réussie", Mqtt.TestAsync(rs).Result);
        using var m = new Mqtt(rs);
        m.Publish("{\"a\":\"1\"}");
        m.Publish("{\"a\":\"2\"}");
        var until = DateTime.UtcNow.AddSeconds(10);
        while (DateTime.UtcNow < until) { lock (got) if (got.Count >= 2) break; Thread.Sleep(50); }
        Assert.Equal([("spyfbbclient/TEST/spot", "{\"a\":\"1\"}"), ("spyfbbclient/TEST/spot", "{\"a\":\"2\"}")], got);
    }

    [Fact]
    public void Password_kept_as_typed_outside_windows()
    {
        if (OperatingSystem.IsWindows())
        {
            var p = Secret.Protect("s3cret");
            Assert.StartsWith("dpapi:", p);
            Assert.Equal("s3cret", Secret.Unprotect(p));
        }
        else Assert.Equal("s3cret", Secret.Unprotect(Secret.Protect("s3cret")));
    }
}
