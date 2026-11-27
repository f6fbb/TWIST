using Twist.App.Services;
using Xunit;

namespace Twist.Tests;

public class CallCheckTests
{
    [Theory]
    [InlineData("F5ZZO", "JN12AX")]
    [InlineData("F1HDI", "JN18CS")]
    [InlineData("F6FBB", null)]
    [InlineData("G4ABC", "IO91")]
    [InlineData("M0ABC", "IO92")]
    [InlineData("MM0ABC", "IO85")]      // Scotland
    [InlineData("GB2RS", "IO91")]       // UK special event
    [InlineData("2E0ABC", "IO90")]
    [InlineData("DL1ABC", "JO62")]
    [InlineData("EA8AB", "IL18")]       // Canary Islands
    [InlineData("TK5AB", "JN41")]       // Corsica
    [InlineData("FR5AB", "LG78")]       // Reunion
    [InlineData("K1ABC", "FN42")]
    [InlineData("KH6AB", "BL11")]       // Hawaii
    [InlineData("VK2ABC", "QF56")]
    [InlineData("JA1ABC", "PM95")]
    [InlineData("IZ1ABC", "JN35")]
    [InlineData("RA3ABC", "KO85")]
    [InlineData("UA9ABC", "MO05")]      // Asiatic Russia
    [InlineData("TM5BBB", "JN18")]      // French special event
    [InlineData("OH0AB", "JP90")]       // Aland
    [InlineData("TE4BSA", null)]        // Costa Rica: a real prefix (a ghost the checks cannot see)
    [InlineData("MQ0EHO", null)]        // seen by a tester, but MQ0... are real UK callsigns (as GR, MO, 2Q...)
    [InlineData("2Q0ABC", "IO75")]
    public void Real_callsigns_pass(string call, string? loc) => Assert.Null(CallCheck.Doubt(call, loc));

    [Theory]
    [InlineData("MZ0ABC", null, "MZ")]          // no UK letter Z
    [InlineData("TW6DXY", "OL30XN", "OL30XN")] // seen by a tester: Wallis & Futuna, a locator in China
    [InlineData("FA1ABC", null, "FA")]
    [InlineData("F5ZZO", "PM95", "PM95")]       // French call, locator in Japan
    [InlineData("QA1ABC", null, "inconnu")]
    public void Ghosts_are_doubtful(string call, string? loc, string why)
    {
        var d = CallCheck.Doubt(call, loc);
        Assert.NotNull(d);
        Assert.Contains(why, d);
    }

    [Fact]
    public void Entity_of_a_callsign()
    {
        Assert.Equal("France", CallCheck.EntityOf("F5ZZO")!.Value.entity.Name);
        Assert.Equal("Scotland", CallCheck.EntityOf("MM0ABC")!.Value.entity.Name);
        Assert.Equal("Canary Islands", CallCheck.EntityOf("EA8AB")!.Value.entity.Name);
    }
}
