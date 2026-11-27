using System.Net;
using System.Net.Http;
using System.Security.Authentication;
using System.Text;
using Twist.App.Services;
using Xunit;

namespace Twist.Tests;

// Aide › Diagnostic fbbspots: each step against a fake server - with
// /ping and /check, without them (the server as it is now), a refused
// key, no name, a certificate refused, a PC clock off.
public class FbbspotsDiagTests
{
    const string Key = "fbb_0123456789abcdef0123456789abcdef01234567";
    static readonly DateTime Now = new(2026, 11, 11, 17, 0, 0, DateTimeKind.Utc);

    sealed class Server(Func<HttpRequestMessage, HttpResponseMessage> answer) : HttpMessageHandler
    {
        public readonly List<string> Asked = new();
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage r, CancellationToken c)
        {
            Asked.Add($"{r.Method} {r.RequestUri!.AbsolutePath}");
            var a = answer(r);
            a.Headers.Date ??= new DateTimeOffset(Now);
            return Task.FromResult(a);
        }
    }
    static HttpResponseMessage J(HttpStatusCode c, string body) => new(c) { Content = new StringContent(body, Encoding.UTF8, "application/json") };
    static Task<IPAddress[]> Dns(string h) => Task.FromResult(new[] { IPAddress.Parse("192.0.2.10") });
    static FbbspotsDiag.Context Ctx(string key = Key, double? mhz = 144.489) =>
        new(true, "https://spots.example.org", key, "F6FBB", true, mhz, null);
    static DiagLine Line(List<DiagLine> l, string title) => l.Single(x => x.Title == title);

    [Fact]
    public async Task All_good_with_ping_and_check()
    {
        var srv = new Server(r => r.RequestUri!.AbsolutePath switch
        {
            "/ping" => J(HttpStatusCode.OK, """{"service":"fbbspots","version":"1.2","time":"2026-11-11T17:00:00Z"}"""),
            "/check" when r.Headers.Authorization?.Parameter == Key =>
                J(HttpStatusCode.OK, """{"callsign":"F6FBB","status":"active","last_spot":"2026-11-11T16:58:02Z"}"""),
            _ => J(HttpStatusCode.NotFound, "{}"),
        });
        var l = await FbbspotsDiag.RunAsync(Ctx(), new HttpClient(srv), Dns, () => Now);
        // (no sender here: the settings not applied - said, not blocking)
        Assert.All(l.Where(x => x.Title != "Envoi"), x => Assert.Equal(DiagState.Ok, x.State));
        Assert.Equal(DiagState.Warn, Line(l, "Envoi").State);
        Assert.Contains("1.2", Line(l, "HTTPS").Detail);
        Assert.Contains("2026-11-11T16:58:02Z", Line(l, "Clé").Detail);
        Assert.DoesNotContain("POST /spot", srv.Asked);                // /check: no spot posted
    }

    [Fact]
    public async Task Server_as_it_is_now_key_checked_by_an_empty_list()
    {
        string? posted = null;
        var srv = new Server(r =>
        {
            if (r.Method == HttpMethod.Post && r.RequestUri!.AbsolutePath == "/spot")
            {
                posted = r.Content!.ReadAsStringAsync().Result;
                return J(HttpStatusCode.OK, """{"accepted":0,"rejected":[]}""");
            }
            return r.RequestUri!.AbsolutePath == "/" ? J(HttpStatusCode.OK, "<html/>") : J(HttpStatusCode.NotFound, "{}");
        });
        var l = await FbbspotsDiag.RunAsync(Ctx(), new HttpClient(srv), Dns, () => Now);
        Assert.Equal(DiagState.Ok, Line(l, "HTTPS").State);
        Assert.Contains("/ping", Line(l, "HTTPS").Detail);
        Assert.Equal(DiagState.Ok, Line(l, "Clé").State);
        Assert.Equal("[]", posted);                                     // nothing recorded
    }

    [Fact]
    public async Task Refused_key_and_station_of_another()
    {
        var refused = new Server(r => r.RequestUri!.AbsolutePath == "/check"
            ? J(HttpStatusCode.Forbidden, """{"error":"station disabled"}""") : J(HttpStatusCode.OK, "{}"));
        var l = await FbbspotsDiag.RunAsync(Ctx(), new HttpClient(refused), Dns, () => Now);
        Assert.Equal(DiagState.Fail, Line(l, "Clé").State);
        Assert.Contains("station disabled", Line(l, "Clé").Detail);

        var other = new Server(r => r.RequestUri!.AbsolutePath == "/check"
            ? J(HttpStatusCode.OK, """{"callsign":"F1HDI","status":"active"}""") : J(HttpStatusCode.OK, "{}"));
        l = await FbbspotsDiag.RunAsync(Ctx(), new HttpClient(other), Dns, () => Now);
        Assert.Equal(DiagState.Warn, Line(l, "Clé").State);
        Assert.Contains("F6FBB", Line(l, "Clé").Detail);
    }

    [Fact]
    public async Task No_name_no_certificate_clock_off_no_frequency()
    {
        var ok = new Server(r => J(HttpStatusCode.OK, """{"callsign":"F6FBB","status":"active"}"""));
        var l = await FbbspotsDiag.RunAsync(Ctx(), new HttpClient(ok), _ => throw new System.Net.Sockets.SocketException(11001), () => Now);
        Assert.Equal(DiagState.Fail, Line(l, "Nom").State);
        Assert.DoesNotContain(l, x => x.Title == "HTTPS");            // stops there

        var tls = new Server(r => throw new HttpRequestException("ssl", new AuthenticationException("remote certificate is invalid")));
        l = await FbbspotsDiag.RunAsync(Ctx(), new HttpClient(tls), Dns, () => Now);
        Assert.Equal(DiagState.Fail, Line(l, "HTTPS").State);
        Assert.Contains("certificat", Line(l, "HTTPS").Detail);

        l = await FbbspotsDiag.RunAsync(Ctx(mhz: null), new HttpClient(ok), Dns, () => Now.AddMinutes(5));
        Assert.Equal(DiagState.Fail, Line(l, "Réception").State);       // no radio frequency: nothing sent
        Assert.Equal(DiagState.Fail, Line(l, "Heure du PC").State);     // 300 s off
        Assert.Contains("+300", Line(l, "Heure du PC").Detail);

        l = await FbbspotsDiag.RunAsync(Ctx(key: "fbb_123"), new HttpClient(ok), Dns, () => Now);
        Assert.Equal(DiagState.Fail, Line(l, "Réglages").State);
        Assert.DoesNotContain(l, x => x.Title == "Clé");
    }
}
