using System.Net;
using System.Net.Http;
using System.Text;
using Twist.App.Services;
using Xunit;

namespace Twist.Tests;

public class FbbspotsTests
{
    // spots.f6fbb.org as its scripts answer (register.php, spot.php)
    sealed class FakeServer(Func<HttpRequestMessage, string, HttpResponseMessage> answer) : HttpMessageHandler
    {
        public List<(string path, string? auth, string body)> Got { get; } = new();
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage r, CancellationToken c)
        {
            var body = r.Content is null ? "" : await r.Content.ReadAsStringAsync(c);
            lock (Got) Got.Add((r.RequestUri!.AbsolutePath, r.Headers.Authorization?.ToString(), body));
            return answer(r, body);
        }
    }
    static HttpResponseMessage Json(HttpStatusCode code, string body) =>
        new(code) { Content = new StringContent(body, Encoding.UTF8, "application/json") };
    const string Key = "fbb_0123456789abcdef0123456789abcdef01234567";

    static void WaitFor(Func<bool> done)
    {
        var until = DateTime.UtcNow.AddSeconds(10);
        while (!done() && DateTime.UtcNow < until) Thread.Sleep(20);
    }

    [Fact]
    public void Key_format()
    {
        Assert.True(Fbbspots.IsKey(Key));
        Assert.True(Fbbspots.IsKey(" " + Key + " "));
        Assert.False(Fbbspots.IsKey("fbb_..."));
        Assert.False(Fbbspots.IsKey(Key.ToUpperInvariant()));
    }

    [Fact]
    public async Task Key_asked_and_the_answer_of_the_server_shown()
    {
        var srv = new FakeServer((r, b) => b.Contains("F5XYZ")
            ? Json(HttpStatusCode.OK, """{"status":"sent","message":"La clé a été envoyée à f5xyz@example.org."}""")
            : Json(HttpStatusCode.Conflict, """{"status":"refused","message":"F6FBB est déjà inscrit avec une autre adresse : contactez l'administrateur."}"""));
        var http = new HttpClient(srv);
        var (ok, msg) = await Fbbspots.RegisterAsync(http, "https://example.org/", "F5XYZ", "JN03AB", "f5xyz@example.org", "fr");
        Assert.True(ok);
        Assert.Equal("La clé a été envoyée à f5xyz@example.org.", msg);
        Assert.Equal("/register", srv.Got[0].path);
        Assert.Equal("""{"callsign":"F5XYZ","locator":"JN03AB","email":"f5xyz@example.org","lang":"fr"}""", srv.Got[0].body);
        (ok, msg) = await Fbbspots.RegisterAsync(http, "https://example.org", "F6FBB", "JN18CS", "x@example.org", "fr");
        Assert.False(ok);
        Assert.Contains("déjà inscrit", msg);
    }

    [Fact]
    public void Spots_sent_with_the_key_kept_while_unreachable()
    {
        bool up = false;
        var srv = new FakeServer((r, b) => up
            ? Json(HttpStatusCode.OK, """{"accepted":2,"rejected":[]}""")
            : throw new HttpRequestException("no route to host"));
        var msgs = new List<(string, bool)>();
        FbbspotsSender.Retry = TimeSpan.FromMilliseconds(200);
        using var s = new FbbspotsSender("https://example.org", Key, new HttpClient(srv));
        s.Message += (m, w) => { lock (msgs) msgs.Add((m, w)); };
        s.Publish("""{"date":"2026-10-29T15:00:00Z","mode":"TWIST"}""");
        s.Publish("""{"date":"2026-10-29T15:15:00Z","mode":"TWIST"}""");
        WaitFor(() => srv.Got.Count >= 2);
        up = true;                                             // the server is back
        WaitFor(() => s.Sent == 2);
        // (the recovery is said just after Sent is counted, from the sender's thread)
        WaitFor(() => { lock (msgs) return msgs.Exists(m => !m.Item2); });
        Assert.Equal(2, s.Sent);
        var last = srv.Got[^1];
        Assert.Equal("/spot", last.path);
        Assert.Equal("Bearer " + Key, last.auth);
        Assert.Equal("""[{"date":"2026-10-29T15:00:00Z","mode":"TWIST"},{"date":"2026-10-29T15:15:00Z","mode":"TWIST"}]""", last.body);
        lock (msgs)
        {
            Assert.Single(msgs, m => m.Item2);                 // the failure said once
            Assert.Contains(msgs, m => !m.Item2);              // and the recovery
        }
    }

    // a refused key pauses the sending (said once, no hammering, the spots
    // kept), tried again later: sent when the key is accepted again
    [Fact]
    public void A_refused_key_pauses_the_sending()
    {
        bool refuse = true;
        var srv = new FakeServer((r, b) => refuse
            ? Json(HttpStatusCode.Forbidden, """{"error":"station disabled for a while"}""")
            : Json(HttpStatusCode.OK, """{"accepted":2,"rejected":[]}"""));
        var msgs = new List<string>();
        var was = FbbspotsSender.RefusedRetry;
        FbbspotsSender.RefusedRetry = TimeSpan.FromMilliseconds(400);
        try
        {
            using var s = new FbbspotsSender("https://example.org", Key, new HttpClient(srv));
            s.Message += (m, _) => { lock (msgs) msgs.Add(m); };
            s.Publish("{}");
            WaitFor(() => s.KeyRefused);
            s.Publish("{}");
            Thread.Sleep(150);
            Assert.Single(srv.Got);                                  // no hammering
            Assert.Equal(2, s.Waiting);                              // both kept
            Assert.Contains("station disabled", s.LastError);
            Assert.Contains("disabled for a while", Assert.Single(msgs));
            refuse = false;
            WaitFor(() => s.Sent == 2);
            Assert.False(s.KeyRefused);
            Assert.Equal(0, s.Waiting);
            Assert.NotNull(s.LastOkUtc);
            Assert.Contains(msgs, m => m.Contains("de nouveau acceptée"));
        }
        finally { FbbspotsSender.RefusedRetry = was; }
    }
}
