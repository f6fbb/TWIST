using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading.Channels;
using MQTTnet;
using MQTTnet.Client;
using MQTTnet.Protocol;
using Twist.App.Lang;

namespace Twist.App.Services;

// A password kept in the settings file: on Windows protected for the
// current user (DPAPI: only this Windows account on this computer can
// read it back), stored as "dpapi:<base64>"; elsewhere as typed (the
// file is in the user's own profile).
public static class Secret
{
    public static string Protect(string plain)
    {
        if (plain == "" || !OperatingSystem.IsWindows()) return plain;
        var b = ProtectedData.Protect(Encoding.UTF8.GetBytes(plain), null, DataProtectionScope.CurrentUser);
        return "dpapi:" + Convert.ToBase64String(b);
    }

    public static string Unprotect(string stored)
    {
        if (!stored.StartsWith("dpapi:")) return stored;
        if (!OperatingSystem.IsWindows()) return "";
        try
        {
            var b = ProtectedData.Unprotect(Convert.FromBase64String(stored[6..]), null, DataProtectionScope.CurrentUser);
            return Encoding.UTF8.GetString(b);
        }
        catch { return ""; }                           // another user or computer
    }
}

// Spots published by MQTT exactly as spyfbbclient does, so that spyspots
// files them unchanged (docs/SPECIFICATION.md, section 4):
//   topic spyfbbclient/<host>/spot
//   {"date":"2026-10-28T13:47:00Z","mode":"TWIST","submode":"L",
//    "frequency":"144492503.2","snr":"-24","payload":"F5ZZO JN12AX",
//    "callsign":"F5ZZO","locator":"JN12AX","deep":"0",
//    "reporter":"F1HDI/P","rgrid":"IN96HA"}
// every value a string; date = start of the frame, UTC, to the second;
// frequency = RF in Hz; mode and submode from the engine's label
// ("TWIST L" -> TWIST / L). spyfbbclient's "sync" (a decoder metric of
// its own) is not sent.
//
// Spots wait in a queue (up to 1000) while the broker is unreachable:
// the client reconnects every 10 s and sends them in order.
public sealed class Mqtt : IDisposable
{
    readonly ReportSettings _s;
    readonly IMqttClient _client;
    readonly Channel<(string topic, string json)> _queue =
        Channel.CreateBounded<(string, string)>(new BoundedChannelOptions(1000) { FullMode = BoundedChannelFullMode.DropOldest });
    readonly CancellationTokenSource _stop = new();
    readonly Task _worker;

    public event Action<string, bool>? Message;        // (text, warning), from a worker thread
    public bool Connected => _client.IsConnected;
    public int Sent { get; private set; }

    public Mqtt(ReportSettings s)
    {
        _s = s;
        _client = new MqttFactory().CreateMqttClient();
        _worker = Task.Run(Run);
    }

    public static string Json(Spot s, string protoLabel, double rfHz, string reporter, string rgrid)
    {
        var parts = protoLabel.Split(' ', 2);
        var o = new Dictionary<string, string>
        {
            ["date"] = s.StartUtc.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'", CultureInfo.InvariantCulture),
            ["mode"] = parts[0],
            ["submode"] = parts.Length > 1 ? parts[1] : "",
            ["frequency"] = rfHz.ToString("0.0", CultureInfo.InvariantCulture),
            ["snr"] = Math.Round(s.Snr).ToString("0", CultureInfo.InvariantCulture),
            ["payload"] = s.Loc == "" ? s.Call : $"{s.Call} {s.Loc}",
            ["callsign"] = s.Call,
        };
        if (s.Loc != "") o["locator"] = s.Loc;
        o["deep"] = s.Deep ? "1" : "0";
        o["reporter"] = reporter;
        o["rgrid"] = rgrid;
        // the program (fbbspots: spots.software; ignored where unknown)
        o["software"] = Report.Software;
        return JsonSerializer.Serialize(o, JsonOpts);
    }

    static readonly JsonSerializerOptions JsonOpts = new()
    {
        Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    // Connects once and disconnects: the "Tester" button of the settings.
    public static async Task<string> TestAsync(ReportSettings s)
    {
        using var c = new MqttFactory().CreateMqttClient();
        var b = new MqttClientOptionsBuilder().WithTcpServer(s.MqttHost.Trim(), s.MqttPort)
            .WithClientId($"twist-test-{Environment.ProcessId}").WithCleanSession().WithTimeout(TimeSpan.FromSeconds(8));
        if (s.MqttUser != "") b = b.WithCredentials(s.MqttUser, Secret.Unprotect(s.MqttPassword));
        if (s.MqttTls) b = b.WithTlsOptions(o => o.UseTls());
        try
        {
            var r = await c.ConnectAsync(b.Build());
            await c.DisconnectAsync();
            return r.ResultCode == MqttClientConnectResultCode.Success ? L.F("connexion à {0}:{1} réussie", s.MqttHost, s.MqttPort) : L.F("refusée : {0}", r.ResultCode);
        }
        catch (Exception e) { return L.T("échec : ") + (e.InnerException?.Message ?? e.Message); }
    }

    public void Publish(string json) => _queue.Writer.TryWrite((_s.MqttTopic, json));

    async Task Run()
    {
        var ct = _stop.Token;
        (string topic, string json)? pending = null;
        while (!ct.IsCancellationRequested)
        {
            try
            {
                if (!_client.IsConnected)
                {
                    var b = new MqttClientOptionsBuilder()
                        .WithTcpServer(_s.MqttHost.Trim(), _s.MqttPort)
                        .WithClientId($"twist-{Environment.MachineName}-{Environment.ProcessId}")
                        .WithCleanSession()
                        .WithTimeout(TimeSpan.FromSeconds(10));
                    if (_s.MqttUser != "") b = b.WithCredentials(_s.MqttUser, Secret.Unprotect(_s.MqttPassword));
                    if (_s.MqttTls) b = b.WithTlsOptions(o => o.UseTls());
                    await _client.ConnectAsync(b.Build(), ct);
                    Message?.Invoke(L.F("MQTT : connecté à {0}:{1}, topic {2}", _s.MqttHost, _s.MqttPort, _s.MqttTopic), false);
                }
                pending ??= await _queue.Reader.ReadAsync(ct);
                var msg = new MqttApplicationMessageBuilder()
                    .WithTopic(pending.Value.topic)
                    .WithPayload(pending.Value.json)
                    .WithQualityOfServiceLevel(MqttQualityOfServiceLevel.AtLeastOnce)
                    .Build();
                var r = await _client.PublishAsync(msg, ct);
                if (!r.IsSuccess) throw new IOException(L.F("publication refusée ({0})", r.ReasonCode));
                pending = null;
                Sent++;
            }
            catch (OperationCanceledException) when (ct.IsCancellationRequested) { break; }
            catch (Exception e)
            {
                Message?.Invoke(L.F("MQTT : {0} ; nouvel essai dans 10 s", e.Message) + (pending is null ? "" : L.T(" (spot gardé)")), true);
                try { await Task.Delay(10000, ct); } catch { break; }
            }
        }
    }

    public void Dispose()
    {
        _stop.Cancel();
        try { _worker.Wait(2000); } catch { }
        try { if (_client.IsConnected) _client.DisconnectAsync().Wait(2000); } catch { }
        _client.Dispose();
    }
}
