using Avalonia.Threading;
using Twist.Native;
using Twist.App.Lang;

namespace Twist.App.ViewModels;

// "Frame in progress" (docs/SPECIFICATION.md, section 2): after each
// pass, the receiver reports the keyed signals present in the last 8 s
// of the zone (twl_keyed). A signal seen on two passes in a row (6 s)
// is shown - in the header of the spots, a dashed label on the waterfall
// at the line where it was first seen - until its frame is decoded, or it is no longer seen.
// Something keyed for longer than the longest frame (41 s) plus a
// margin is not a frame (CW identification, another mode, a carrier
// that the detector did not reject): it is hidden until it stops.
public partial class MainWindowViewModel
{
    sealed class LiveTrack
    {
        public double Freq;
        public DateTime First, Last;
        public int Passes;
        public bool TooLong;
        public bool Pi4;                       // a PI4 frame (Freq: its carrier)
        public Controls.WaterfallMark? Mark;
    }

    readonly List<LiveTrack> _live = new();
    readonly List<(double freq, DateTime until)> _decodedNear = new();   // tails of decoded frames
    // the tones of the PI4 stations heard (4 tones, 234.375 Hz apart): the
    // highest can fall in the OOK zone (F5ZZO at 798 Hz: 1384 Hz), and its
    // on/off pattern is no frame in progress - for 3 minutes after each
    // PI4 frame of that station (they send every minute)
    readonly List<(double freq, DateTime until)> _pi4Tones = new();

    // (times: of the audio - a spot's end, a pass's newest sample - not of
    // the wall clock, which a fast replay leaves far behind)
    void Pi4Heard(double carrier, DateTime end)
    {
        var until = end.AddMinutes(3);
        double tone0 = carrier - 234.375 / 2;
        _pi4Tones.RemoveAll(t => Math.Abs(t.freq - tone0) < 1 + 3 * 234.375 && Math.Abs((t.freq - tone0) % 234.375) < 5);
        for (int k = 0; k < 4; k++) _pi4Tones.Add((tone0 + k * 234.375, until));
        foreach (var t in _live.Where(x => _pi4Tones.Any(p => Math.Abs(p.freq - x.Freq) < LiveSep)).ToList()) Drop(t);
        ShowLive(end);
    }
    const double LiveSep = 10, LongestFrame = 321 * 0.128 + 8, LongestPi4 = 146 / 6.0 + 8;

    // PI4 frames in progress (Services/Pi4Activity), at their carrier; their
    // tones are no OOK frame in progress meanwhile
    void OnPi4Active(List<double> carriers, DateTime now)
    {
        _decodedNear.RemoveAll(d => d.until < now);
        foreach (var c in carriers)
        {
            double tone0 = c - Services.Pi4Activity.CarrierOffset;
            for (int k = 0; k < 4; k++) _pi4Tones.Add((tone0 + k * Services.Pi4Activity.Spacing, now.AddSeconds(Services.Receiver.KeyedWindow + 4)));
            foreach (var o in _live.Where(x => !x.Pi4 && _pi4Tones.Any(p => Math.Abs(p.freq - x.Freq) < LiveSep)).ToList()) Drop(o);
            if (_decodedNear.Any(d => Math.Abs(d.freq - c) < LiveSep)) continue;
            var t = _live.FirstOrDefault(x => x.Pi4 && Math.Abs(x.Freq - c) < LiveSep);
            if (t is null) { t = new LiveTrack { Freq = c, First = now, Pi4 = true }; _live.Add(t); }
            t.Freq = 0.7 * t.Freq + 0.3 * c;
            t.Last = now;
            t.Passes++;
            if ((now - t.First).TotalSeconds > LongestPi4) t.TooLong = true;
        }
        Gone(now, pi4: true);
        ShowLive(now);
    }

    // not seen on the last two passes
    void Gone(DateTime now, bool pi4)
    {
        foreach (var t in _live.Where(x => x.Pi4 == pi4 && (now - x.Last).TotalSeconds > 2.2 * (_rx?.PeriodSeconds ?? 3)).ToList())
            Drop(t);
    }

    void OnKeyed(TwlKeyed[] hits, DateTime now)
    {
        _decodedNear.RemoveAll(d => d.until < now);
        _pi4Tones.RemoveAll(d => d.until < now);
        foreach (var h in hits)
        {
            if (_decodedNear.Any(d => Math.Abs(d.freq - h.Freq) < LiveSep)) continue;
            if (_pi4Tones.Any(d => Math.Abs(d.freq - h.Freq) < LiveSep)) continue;
            var t = _live.FirstOrDefault(x => !x.Pi4 && Math.Abs(x.Freq - h.Freq) < LiveSep);
            if (t is null) { t = new LiveTrack { Freq = h.Freq, First = now }; _live.Add(t); }
            t.Freq = 0.7 * t.Freq + 0.3 * h.Freq;
            t.Last = now;
            t.Passes++;
            if ((now - t.First).TotalSeconds > LongestFrame) t.TooLong = true;
        }
        Gone(now, pi4: false);
        ShowLive(now);
    }

    void Drop(LiveTrack t)
    {
        if (t.Mark is not null) Marks.Remove(t.Mark);
        _live.Remove(t);
    }

    // a decoded frame, ended at `end`: its signal is no longer "in
    // progress", nor its tail while it is in the window of the detectors
    void LiveDecoded(double freq, DateTime end)
    {
        foreach (var t in _live.Where(x => Math.Abs(x.Freq - freq) < LiveSep).ToList()) Drop(t);
        _decodedNear.Add((freq, end.AddSeconds(Services.Receiver.KeyedWindow + 4)));
        ShowLive(end);
    }

    void ShowLive(DateTime now)
    {
        var shown = _live.Where(t => t.Passes >= 2 && !t.TooLong).OrderBy(t => t.Freq).ToList();
        foreach (var t in _live)
        {
            bool show = shown.Contains(t);
            if (t.Mark is not null) { Marks.Remove(t.Mark); t.Mark = null; }
            if (show)
            {
                // (at the line where it was first seen: it goes down with the picture)
                // (Réglages › Interface: its audio frequency instead)
                string label = _cfg.Interface.Waterfall.LiveFrequency
                    ? (t.Pi4 ? "PI4 · " : "") + $"{t.Freq:0} Hz"
                    : L.T(t.Pi4 ? "réception PI4 en cours…" : "réception en cours…");
                t.Mark = new Controls.WaterfallMark(t.Freq, t.First, label, true, InProgress: true, Pi4: t.Pi4);
                Marks.Add(t.Mark);
            }
        }
        ReceivingNow = shown.Count > 0;
        OnPropertyChanged(nameof(WfInfo));      // the folded waterfall's title says it too
        // (no "since": a weak frame is only noticed some seconds after it began)
        ReceivingText = string.Join("   ", shown.Select(t => t.Pi4
            ? L.F("réception PI4 en cours · {0:0} Hz · repérée à {1:HH:mm:ss}", t.Freq, t.First)
            : L.F("réception en cours · {0:0} Hz · signal manipulé repéré à {1:HH:mm:ss}", t.Freq, t.First)));
    }

    void ClearLive()
    {
        foreach (var t in _live.ToList()) Drop(t);
        _decodedNear.Clear();
        ReceivingNow = false;
        ReceivingText = "";
    }
}
