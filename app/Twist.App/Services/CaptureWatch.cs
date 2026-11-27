namespace Twist.App.Services;

// Watches a live capture (Receiver.LiveLoop, every 0.1 s): the audio
// captured so far (twl_audio stats, s) must go on growing.
// - Woke: a sleep of 0.1 s of the loop lasted more than WakeGap s (the
//   computer slept; a long pass does not count): the
//   sound card's stream does not come back by itself after it wakes up
//   (Windows), the device is opened again at once;
// - Stalled: no audio for StallSeconds while the loop runs (the device
//   unplugged, a driver gone): opened again, then every StallSeconds.
public sealed class CaptureWatch(double seconds, DateTime now)
{
    public const double StallSeconds = 5, WakeGap = 10;
    public enum Verdict { Ok, Woke, Stalled }

    double _seconds = seconds;
    DateTime _advanced = now;
    public Verdict Last { get; private set; }

    // slept: how long the loop's Thread.Sleep(100) really took, s
    public Verdict Check(double seconds, DateTime now, double slept)
    {
        if (slept > WakeGap) return Last = Verdict.Woke;
        if (seconds != _seconds) { _seconds = seconds; _advanced = now; return Last = Verdict.Ok; }
        return Last = (now - _advanced).TotalSeconds > StallSeconds ? Verdict.Stalled : Verdict.Ok;
    }
}
