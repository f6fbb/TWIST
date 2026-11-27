using System.Net;
using System.Net.Http;
using Twist.App.Services;
using Xunit;

namespace Twist.Tests;

public class LocatorTests
{
    // answers as spots.f6fbb.org (fbbspots) would, and counts the questions
    sealed class FakeApi(Func<HttpRequestMessage, HttpResponseMessage> answer) : HttpMessageHandler
    {
        public List<string> Asked { get; } = new();
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage r, CancellationToken c)
        {
            lock (Asked) Asked.Add(r.RequestUri!.Query);
            return Task.FromResult(answer(r));
        }
    }
    static HttpResponseMessage Json(HttpStatusCode code, string body) =>
        new(code) { Content = new StringContent(body, System.Text.Encoding.UTF8, "application/json") };

    static string TempFile() => Path.Combine(Path.GetTempPath(), "twist-loc-" + Guid.NewGuid().ToString("N"), "locators.json");

    [Fact]
    public void List_of_the_settings_with_or_without_locators()
    {
        var l = LocatorBook.ParseList("f5zzo jn12ax, F1HDI;F6XYZ=IN96ha  F5ZRV, F5ZZO");
        Assert.Equal([("F5ZZO", "JN12AX"), ("F1HDI", null), ("F6XYZ", "IN96HA"), ("F5ZRV", null)], l);
        Assert.Equal("F5ZZO JN12AX, F1HDI, F6XYZ IN96HA, F5ZRV", LocatorBook.FormatList(l));
        Assert.Equal("F5ZZO,F1HDI,F6XYZ,F5ZRV", LocatorBook.CallsOnly("F5ZZO JN12AX, F1HDI, F6XYZ IN96ha F5ZRV"));
        Assert.Equal("F5ZZO, F1HDI", LocatorBook.FormatList(LocatorBook.ParseList("F5ZZO, F1HDI")));   // a list of 0.15
        Assert.Equal("F5ZZO JN12", LocatorBook.FormatList(LocatorBook.ParseList("F5ZZO JN12")));
    }

    [Fact]
    public void Order_list_then_frames_then_fbbspots_and_kept_on_disk()
    {
        var file = TempFile();
        var b = new LocatorBook(file) { AskFbbspots = false };
        Assert.Null(b.Find("F5ZZO"));
        b.Heard("F5ZZO", "JN12AX");
        // found, not received in this frame: lower case (as on the fbbalise site)
        Assert.Equal(new LocatorFound("jn12ax", LocSource.Heard, b.Find("F5ZZO")!.Utc), b.Find("f5zzo"));
        b.SetList("F5ZZO JN12AB");
        Assert.Equal(("jn12ab", LocSource.List), (b.Find("F5ZZO")!.Locator, b.Find("F5ZZO")!.Source));
        b.SetList("");

        var again = new LocatorBook(file) { AskFbbspots = false };    // the next start
        Assert.Equal(("jn12ax", LocSource.Heard), (again.Find("F5ZZO")!.Locator, again.Find("F5ZZO")!.Source));
    }

    [Fact]
    public async Task Fbbspots_asked_once_per_callsign()
    {
        var api = new FakeApi(r => r.RequestUri!.Query.Contains("call=F5ZZO")
            ? Json(HttpStatusCode.OK, """{"call":"F5ZZO","locator":"JN12ax","source":"decoded","band_mhz":144,"nspots":3,"approximate":false}""")
            : r.RequestUri!.Query.Contains("call=F1HDI%2FP")
                ? Json(HttpStatusCode.OK, """{"call":"F1HDI/P","locator":"JN18cs","source":"decoded","band_mhz":144,"nspots":1,"approximate":true}""")
                : Json(HttpStatusCode.NotFound, """{"call":"XX9XX","locator":null}"""));
        var file = TempFile();
        var b = new LocatorBook(file) { Http = new HttpClient(api), Api = "https://example.org/locator" };

        var f = await b.LookupAsync("F5ZZO", 144);
        Assert.Equal(("jn12ax", LocSource.Fbbspots, false), (f!.Locator, f.Source, f.Approximate));
        Assert.Equal("?call=F5ZZO&band=144", api.Asked[0]);
        Assert.NotNull(await b.LookupAsync("F5ZZO", 144));              // kept: not asked again
        Assert.Single(api.Asked);

        Assert.True((await b.LookupAsync("F1HDI/P", 0))!.Approximate);   // its base callsign's
        Assert.EndsWith("call=F1HDI%2FP", api.Asked[1]);

        Assert.Null(await b.LookupAsync("XX9XX", 144));                 // unknown: not asked again soon
        Assert.Null(await b.LookupAsync("XX9XX", 144));
        Assert.Equal(3, api.Asked.Count);

        var again = new LocatorBook(file) { AskFbbspots = false };          // fbbspots' answers kept on disk
        Assert.Equal(LocSource.Fbbspots, again.Find("F5ZZO")!.Source);
    }

    [Fact]
    public async Task Fbbspots_unreachable_said_once_then_left_alone()
    {
        var api = new FakeApi(_ => throw new HttpRequestException("no route to host"));
        var msgs = new List<string>();
        var b = new LocatorBook(null) { Http = new HttpClient(api) };
        b.Message += msgs.Add;
        Assert.Null(await b.LookupAsync("F5ZZO", 144));
        Assert.Null(await b.LookupAsync("F6XYZ", 144));                 // within 10 minutes: not tried
        Assert.Single(api.Asked);
        Assert.Single(msgs);
        Assert.Contains("F5ZZO", msgs[0]);

        var off = new LocatorBook(null) { Http = new HttpClient(api), AskFbbspots = false };
        Assert.Null(await off.LookupAsync("F5ZZO", 144));               // not allowed: never asked
        Assert.Single(api.Asked);
    }
}
