using System.Net;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using Twist.App.Services;
using Xunit;

namespace Twist.Tests;

// New versions (Services/Updates): latest.json read, the installer that
// fits chosen, downloaded and checked (size, SHA-256) - from a fake server.
public class UpdatesTests
{
    static readonly byte[] Setup = Encoding.ASCII.GetBytes(new string('S', 5000));
    static readonly byte[] Update = Encoding.ASCII.GetBytes(new string('U', 1200));
    static string Sha(byte[] b) => Convert.ToHexString(SHA256.HashData(b)).ToLowerInvariant();

    static string Latest(string setupSha, string file = "TWIST-Console-0.25.0-setup.exe") => $$"""
        { "version": "0.25.0", "date": "2026-11-10", "notes": "### Added\n- update check",
          "page": "https://update.f6fbb.org/twist/",
          "windows": { "setup": { "file": "{{file}}", "size": {{Setup.Length}}, "sha256": "{{setupSha}}" },
                       "update": { "file": "TWIST-Console-0.25.0-mise-a-jour.exe", "size": {{Update.Length}}, "sha256": "{{Sha(Update)}}", "base": "0.22.0" } } }
        """;

    sealed class Server(Dictionary<string, byte[]> files) : HttpMessageHandler
    {
        public readonly List<string> Asked = new();
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage r, CancellationToken ct)
        {
            Asked.Add(r.RequestUri!.AbsolutePath);
            var name = r.RequestUri.AbsolutePath.Split('/').Last();
            return Task.FromResult(files.TryGetValue(name, out var b)
                ? new HttpResponseMessage(HttpStatusCode.OK) { Content = new ByteArrayContent(b) }
                : new HttpResponseMessage(HttpStatusCode.NotFound));
        }
    }

    static (Updates u, Server s) With(string latest, byte[]? setup = null)
    {
        var s = new Server(new()
        {
            ["latest.json"] = Encoding.UTF8.GetBytes(latest),
            ["TWIST-Console-0.25.0-setup.exe"] = setup ?? Setup,
            ["TWIST-Console-0.25.0-mise-a-jour.exe"] = Update,
        });
        return (new Updates { Http = new HttpClient(s), Url = "https://update.example.org/twist/latest.json" }, s);
    }

    [Fact]
    public async Task Newest_version_read_and_the_right_installer_chosen()
    {
        var (u, _) = With(Latest(Sha(Setup)));
        var i = await u.CheckAsync();
        Assert.NotNull(i);
        Assert.Equal("0.25.0", i!.Version);
        Assert.Contains("update check", i.Notes);
        Assert.True(Updates.Newer(i.Version, "0.24.1"));
        Assert.False(Updates.Newer(i.Version, "0.25.0"));
        Assert.False(Updates.Newer("0.9.0", "0.10.0"));            // numbers, not text
        // pre-releases (semantic versioning): rc.1 < rc.2 < rc.10 < the release
        Assert.True(Updates.Newer("1.0.0-rc.1", "0.27.1"));
        Assert.True(Updates.Newer("1.0.0-rc.2", "1.0.0-rc.1"));
        Assert.True(Updates.Newer("1.0.0-rc.10", "1.0.0-rc.2"));
        Assert.True(Updates.Newer("1.0.0", "1.0.0-rc.10"));
        Assert.False(Updates.Newer("1.0.0-rc.1", "1.0.0"));
        Assert.False(Updates.Newer("1.0.0-rc.1", "1.0.0-rc.1"));
        Assert.False(Updates.Newer("1.0.0-", "0.1.0"));              // not a version
        Assert.Equal("TWIST-Console-0.25.0-mise-a-jour.exe", Updates.Choose(i, "0.24.1")!.File);
        Assert.Equal("TWIST-Console-0.25.0-mise-a-jour.exe", Updates.Choose(i, "0.22.0")!.File);
        Assert.Equal("TWIST-Console-0.25.0-setup.exe", Updates.Choose(i, "0.21.1")!.File);   // older than the update's base
    }

    [Fact]
    public async Task Downloaded_and_checked()
    {
        var dir = Path.Combine(Path.GetTempPath(), "twist-upd-" + Guid.NewGuid().ToString("N"));
        var (u, s) = With(Latest(Sha(Setup)));
        var i = (await u.CheckAsync())!;
        var seen = new List<double>();
        var path = await u.DownloadAsync(i.Setup!, dir, new SyncProgress(seen));
        Assert.Equal(Setup, File.ReadAllBytes(path));
        Assert.Equal(1.0, seen.Last(), 3);
        Assert.Contains("/twist/TWIST-Console-0.25.0-setup.exe", s.Asked);   // beside latest.json
        Directory.Delete(dir, true);
    }

    [Fact]
    public async Task Altered_file_refused_and_removed()
    {
        var dir = Path.Combine(Path.GetTempPath(), "twist-upd-" + Guid.NewGuid().ToString("N"));
        var bad = (byte[])Setup.Clone();
        bad[100] = (byte)'X';
        var (u, _) = With(Latest(Sha(Setup)), bad);
        var i = (await u.CheckAsync())!;
        var e = await Assert.ThrowsAsync<InvalidDataException>(() => u.DownloadAsync(i.Setup!, dir));
        Assert.Contains("SHA-256", e.Message);
        Assert.False(File.Exists(Path.Combine(dir, i.Setup!.File)));
        // and a file longer than announced
        var (u2, _) = With(Latest(Sha(Setup)), [.. Setup, (byte)'+']);
        await Assert.ThrowsAsync<InvalidDataException>(async () => await u2.DownloadAsync((await u2.CheckAsync())!.Setup!, dir));
        Directory.Delete(dir, true);
    }

    [Theory]
    [InlineData("../evil.exe")]
    [InlineData("sub/evil.exe")]
    [InlineData("C:\\\\evil.exe")]
    public void Only_a_file_beside_latest_json(string file)
    {
        var i = Updates.Parse(Latest(Sha(Setup), file));
        Assert.NotNull(i);
        Assert.Null(i!.Setup);
    }

    [Fact]
    public void Nonsense_ignored()
    {
        Assert.Null(Updates.Parse("""{ "version": "soon" }"""));
        Assert.Null(Updates.Parse(Latest("1234")).Setup);           // not a SHA-256
    }

    sealed class SyncProgress(List<double> seen) : IProgress<double>
    {
        public void Report(double v) => seen.Add(v);
    }
}
