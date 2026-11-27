using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using System.Threading.Channels;
using Twist.App.Lang;

namespace Twist.App.Services;

// fbbspots (spots.f6fbb.org): the key of a station, asked by email, and
// the spots sent with it over HTTPS, in the JSON of spyfbbclient
// (Mqtt.Json); the server republishes them for spyspots. No password in
// the program: each station has its own key, the server names the
// reporter after it.
//   POST /register {"callsign","locator","email","lang"}
//        200 sent / 202 pending (validated by the administrator) / 4xx why
//   POST /spot     Authorization: Bearer fbb_...   one spot or a list (100 at most)
//        200 {"accepted":n,"rejected":[{"index":i,"reason":"..."}]}
//        401 unknown key, 403 station disabled or waiting
public static class Fbbspots
{
    public const string DefaultSite = "https://spots.f6fbb.org";
    public static string UserAgent => "TWIST-Console/" + Report.Version;

    public static bool IsKey(string k) => System.Text.RegularExpressions.Regex.IsMatch(k.Trim(), "^fbb_[0-9a-f]{40}$");

    // (ok, message from the server, or why it failed)
    public static async Task<(bool ok, string message)> RegisterAsync(HttpClient http, string site, string call, string loc,
                                                                    string email, string lang)
    {
        try
        {
            var body = JsonSerializer.Serialize(new { callsign = call, locator = loc, email, lang });
            using var req = new HttpRequestMessage(HttpMethod.Post, site.TrimEnd('/') + "/register")
            { Content = new StringContent(body, Encoding.UTF8, "application/json") };
            req.Headers.UserAgent.ParseAdd(UserAgent);
            using var r = await http.SendAsync(req).ConfigureAwait(false);
            var text = await r.Content.ReadAsStringAsync().ConfigureAwait(false);
            string? msg = null;
            try { msg = JsonDocument.Parse(text).RootElement.GetProperty("message").GetString(); } catch { }
            return (r.IsSuccessStatusCode, msg ?? L.F("réponse inattendue du serveur ({0})", (int)r.StatusCode));
        }
        catch (Exception e) { return (false, L.T("serveur injoignable : ") + (e.InnerException?.Message ?? e.Message)); }
    }
}

// The spots of this station, sent in order; while the server cannot be
// reached they wait (up to 1000), sent again every 30 s, by lists of 100.
// A refused key (401, 403) pauses the sending: said once, tried again
// every 10 minutes (a station disabled for a while, the server's own
// trouble), the spots kept; a new key in the settings starts a new sender.
// (0.26 and before: stopped until the settings changed - a tester's
// sending stopped for good after one refusal.)
// Its state is kept for the diagnostic (Aide › Diagnostic fbbspots).
public sealed class FbbspotsSender : IDisposable
{
    readonly string _site, _key;
    readonly HttpClient _http;
    readonly Channel<string> _queue =
        Channel.CreateBounded<string>(new BoundedChannelOptions(1000) { FullMode = BoundedChannelFullMode.DropOldest });
    readonly CancellationTokenSource _stop = new();
    readonly Task _worker;
    public static TimeSpan Retry { get; set; } = TimeSpan.FromSeconds(30);
    public static TimeSpan RefusedRetry { get; set; } = TimeSpan.FromMinutes(10);

    public event Action<string, bool>? Message;        // (text, warning), from a worker thread
    public int Sent { get; private set; }
    public bool KeyRefused { get; private set; }
    // the state, for the diagnostic
    public DateTime? LastOkUtc { get; private set; }
    public DateTime? LastErrorUtc { get; private set; }
    public string LastError { get; private set; } = "";
    int _inFlight;
    public int Waiting => _queue.Reader.Count + Volatile.Read(ref _inFlight);
    public string Site => _site;

    public FbbspotsSender(string site, string key, HttpClient? http = null)
    {
        _site = site.TrimEnd('/');
        _key = key.Trim();
        _http = http ?? new HttpClient { Timeout = TimeSpan.FromSeconds(15) };
        // (also on each request below: an HttpClient given by the tests has none)
        if (http is null) _http.DefaultRequestHeaders.UserAgent.ParseAdd(Fbbspots.UserAgent);
        _worker = Task.Run(Run);
    }

    public void Publish(string json) => _queue.Writer.TryWrite(json);

    async Task Run()
    {
        var ct = _stop.Token;
        var batch = new List<string>();
        bool failing = false, said = false;
        while (!ct.IsCancellationRequested)
        {
            try
            {
                if (batch.Count == 0) batch.Add(await _queue.Reader.ReadAsync(ct));
                while (batch.Count < 100 && _queue.Reader.TryRead(out var more)) batch.Add(more);
                Volatile.Write(ref _inFlight, batch.Count);
                using var req = new HttpRequestMessage(HttpMethod.Post, _site + "/spot")
                { Content = new StringContent("[" + string.Join(",", batch) + "]", Encoding.UTF8, "application/json") };
                req.Headers.Authorization = new AuthenticationHeaderValue("Bearer", _key);
                req.Headers.UserAgent.ParseAdd(Fbbspots.UserAgent);
                using var r = await _http.SendAsync(req, ct);
                var text = await r.Content.ReadAsStringAsync(ct);
                if (r.StatusCode is HttpStatusCode.Unauthorized or HttpStatusCode.Forbidden)
                {
                    LastError = L.F("clé refusée ({0}) : {1}", (int)r.StatusCode, Reason(text));
                    LastErrorUtc = DateTime.UtcNow;
                    if (!KeyRefused)
                        Message?.Invoke(L.F("fbbspots : clé refusée ({0}) ; spots gardés, nouvel essai toutes les {1:0} min (Réglages › Rapports, Aide › Diagnostic fbbspots)",
                                            Reason(text), RefusedRetry.TotalMinutes), true);
                    KeyRefused = true;
                    await Task.Delay(RefusedRetry, ct);
                    continue;
                }
                if (!r.IsSuccessStatusCode) throw new IOException(L.F("réponse {0} : {1}", (int)r.StatusCode, Reason(text)));
                using (var doc = JsonDocument.Parse(text))
                {
                    var root = doc.RootElement;
                    Sent += root.GetProperty("accepted").GetInt32();
                    foreach (var x in root.GetProperty("rejected").EnumerateArray())
                        Message?.Invoke(L.F("fbbspots : spot refusé ({0})", x.GetProperty("reason").GetString()), true);
                }
                batch.Clear();
                Volatile.Write(ref _inFlight, 0);
                LastOkUtc = DateTime.UtcNow;
                if (KeyRefused) Message?.Invoke(L.T("fbbspots : clé de nouveau acceptée, spots en attente envoyés"), false);
                else if (failing) Message?.Invoke(L.T("fbbspots : de nouveau joignable, spots en attente envoyés"), false);
                else if (!said) Message?.Invoke(L.F("fbbspots : spots envoyés à {0}", new Uri(_site).Host), false);
                failing = false; said = true; KeyRefused = false;
            }
            catch (OperationCanceledException) when (ct.IsCancellationRequested) { break; }
            catch (Exception e)
            {
                LastError = e.InnerException?.Message ?? e.Message;
                LastErrorUtc = DateTime.UtcNow;
                if (!failing)
                    Message?.Invoke(L.F("fbbspots : {0} ; nouvel essai toutes les {1:0} s (spots gardés)", LastError, Retry.TotalSeconds), true);
                failing = true;
                try { await Task.Delay(Retry, ct); } catch { break; }
            }
        }
    }

    public static string Reason(string json)
    {
        try { return JsonDocument.Parse(json).RootElement.GetProperty("error").GetString() ?? json; }
        catch { return json.Length > 120 ? json[..120] : json; }
    }

    public void Dispose()
    {
        _stop.Cancel();
        try { _worker.Wait(2000); } catch { }
    }
}
