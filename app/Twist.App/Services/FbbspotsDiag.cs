using System.Diagnostics;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Security.Authentication;
using System.Text;
using System.Text.Json;
using Twist.App.Lang;

namespace Twist.App.Services;

public enum DiagState { Ok, Warn, Fail, Info }
public sealed record DiagLine(DiagState State, string Title, string Detail)
{
    public string Mark => State switch { DiagState.Ok => "✔", DiagState.Warn => "⚠", DiagState.Fail => "✘", _ => "•" };
    public override string ToString() => $"{Mark} {Title} : {Detail}";
}

// Aide › Diagnostic fbbspots (and Réglages › Rapports › Diagnostic): from
// the settings to the server, in order, each step saying what it found -
// the testers' "it does not send any more" answered without guessing:
//   1. the settings: sending on, a key of the right form, a callsign;
//   2. what the program needs to send a spot: a live source, the radio's
//      frequency (a spot without it is not sent);
//   3. the sender: spots sent, waiting, its last success and last error;
//   4. the name of the server (DNS);
//   5. HTTPS: GET /ping (the service, its version; GET / if the server
//      has none yet), the time it took; a certificate refused, no answer;
//   6. the PC's clock, against the server's (Date): spots are dated by it;
//   7. the key: GET /check (who the key is, its state, its last spot);
//      if the server has none yet, an empty list posted to /spot (a key
//      accepted answers 200, nothing is recorded).
// docs/FBBSPOTS.md: the protocol, /ping and /check included.
public static class FbbspotsDiag
{
    public sealed record Context(bool Enabled, string Site, string Key, string Callsign, bool Live, double? RadioMHz,
                                 FbbspotsSender? Sender);

    public static async Task<List<DiagLine>> RunAsync(Context c, HttpClient http,
                                                      Func<string, Task<IPAddress[]>>? dns = null, Func<DateTime>? now = null)
    {
        var lines = new List<DiagLine>();
        var utc = now ?? (() => DateTime.UtcNow);
        dns ??= h => Dns.GetHostAddressesAsync(h);
        string site = (c.Site is { Length: > 0 } s ? s : Fbbspots.DefaultSite).TrimEnd('/');
        string key = c.Key.Trim();

        // 1. settings
        if (!c.Enabled) lines.Add(new(DiagState.Fail, L.T("Réglages"), L.T("envoi à fbbspots désactivé (Réglages › Rapports)")));
        else if (key == "") lines.Add(new(DiagState.Fail, L.T("Réglages"), L.T("pas de clé (Réglages › Rapports › Demander une clé…)")));
        else if (!Fbbspots.IsKey(key)) lines.Add(new(DiagState.Fail, L.T("Réglages"), L.T("clé de forme inattendue : fbb_ suivi de 40 caractères 0-9 a-f (copiée en entier ?)")));
        else if (c.Callsign == "") lines.Add(new(DiagState.Fail, L.T("Réglages"), L.T("indicatif de la station à régler (Réglages › Station)")));
        else lines.Add(new(DiagState.Ok, L.T("Réglages"), L.F("envoi activé, clé {0}…{1}, station {2}", key[..8], key[^4..], c.Callsign)));

        // 2. what a spot needs
        if (!c.Live) lines.Add(new(DiagState.Warn, L.T("Réception"), L.T("pas d'entrée audio en direct : les spots d'un fichier rejoué ne sont pas envoyés")));
        else if (c.RadioMHz is null) lines.Add(new(DiagState.Fail, L.T("Réception"), L.T("fréquence de la radio inconnue : aucun spot n'est envoyé (CAT, ou fréquence à saisir)")));
        else lines.Add(new(DiagState.Ok, L.T("Réception"), L.F("entrée audio en direct, radio à {0:0.000000} MHz", c.RadioMHz)));

        // 3. the sender
        if (c.Sender is { } f)
        {
            string Ago(DateTime t) => L.F("{0:HH:mm:ss} UTC, il y a {1}", t, Span(utc() - t));
            var detail = L.F("{0} envoyés depuis le lancement, {1} en attente", f.Sent, f.Waiting)
                + (f.LastOkUtc is DateTime ok ? L.F(" ; dernier envoi réussi {0}", Ago(ok)) : L.T(" ; aucun envoi réussi"))
                + (f.LastErrorUtc is DateTime e ? L.F(" ; dernière erreur {0} : {1}", Ago(e), f.LastError) : "");
            var st = f.KeyRefused ? DiagState.Fail
                   : f.LastErrorUtc is DateTime le && (f.LastOkUtc is not DateTime lo || le > lo) ? DiagState.Warn
                   : DiagState.Ok;
            lines.Add(new(st, L.T("Envoi"), detail));
        }
        else if (c.Enabled && Fbbspots.IsKey(key))
            lines.Add(new(DiagState.Warn, L.T("Envoi"), L.T("pas d'envoi en cours (réglages pas encore enregistrés ?)")));

        // 4. the server's name
        string host;
        try { host = new Uri(site).Host; }
        catch { lines.Add(new(DiagState.Fail, L.T("Serveur"), L.F("adresse invalide : {0}", site))); return lines; }
        try
        {
            var a = await dns(host).WaitAsync(TimeSpan.FromSeconds(10)).ConfigureAwait(false);
            lines.Add(a.Length == 0 ? new(DiagState.Fail, L.T("Nom"), L.F("{0} : aucune adresse", host))
                                    : new(DiagState.Ok, L.T("Nom"), $"{host} → " + string.Join(", ", a.Take(4))));
            if (a.Length == 0) return lines;
        }
        catch (Exception e)
        {
            lines.Add(new(DiagState.Fail, L.T("Nom"), L.F("{0} introuvable ({1}) : connexion Internet, DNS ?", host, e.InnerException?.Message ?? e.Message)));
            return lines;
        }

        // 5. HTTPS, 6. the clock
        DateTimeOffset? serverDate = null;
        try
        {
            var sw = Stopwatch.StartNew();
            using var r = await Get(http, site + "/ping", null).ConfigureAwait(false);
            sw.Stop();
            serverDate = r.Headers.Date;
            if (r.StatusCode == HttpStatusCode.NotFound)
            {
                using var r2 = await Get(http, site + "/", null).ConfigureAwait(false);
                serverDate ??= r2.Headers.Date;
                lines.Add(new((int)r2.StatusCode < 500 ? DiagState.Ok : DiagState.Warn, L.T("HTTPS"),
                    L.F("{0} répond ({1}) en {2} ms (le serveur ne propose pas encore /ping)", host, (int)r2.StatusCode, sw.ElapsedMilliseconds)));
            }
            else if (r.IsSuccessStatusCode)
            {
                string ver = "";
                try { using var d = JsonDocument.Parse(await r.Content.ReadAsStringAsync().ConfigureAwait(false));
                      if (d.RootElement.TryGetProperty("version", out var v)) ver = " · " + v; }
                catch { }
                lines.Add(new(DiagState.Ok, L.T("HTTPS"), L.F("{0} répond en {1} ms", host, sw.ElapsedMilliseconds) + ver));
            }
            else lines.Add(new(DiagState.Warn, L.T("HTTPS"), L.F("{0} répond {1} à /ping", host, (int)r.StatusCode)));
        }
        catch (Exception e)
        {
            lines.Add(new(DiagState.Fail, L.T("HTTPS"), Why(e)));
            return lines;
        }
        if (serverDate is DateTimeOffset sd)
        {
            double off = (utc() - sd.UtcDateTime).TotalSeconds;
            lines.Add(Math.Abs(off) > 120 ? new(DiagState.Fail, L.T("Heure du PC"), L.F("décalée de {0:+0;-0} s par rapport au serveur : les spots sont datés faux (synchroniser l'horloge de Windows)", off))
                    : Math.Abs(off) > 5 ? new(DiagState.Warn, L.T("Heure du PC"), L.F("décalée de {0:+0;-0} s par rapport au serveur (à synchroniser)", off))
                    : new(DiagState.Ok, L.T("Heure du PC"), L.F("à l'heure (écart {0:+0;-0} s avec le serveur, à la seconde près)", off)));
        }

        // 7. the key
        if (!Fbbspots.IsKey(key)) return lines;
        try
        {
            using var r = await Get(http, site + "/check", key).ConfigureAwait(false);
            var text = await r.Content.ReadAsStringAsync().ConfigureAwait(false);
            if (r.StatusCode == HttpStatusCode.NotFound)
            {
                // no /check yet: an empty list to /spot (nothing recorded)
                using var req = new HttpRequestMessage(HttpMethod.Post, site + "/spot") { Content = new StringContent("[]", Encoding.UTF8, "application/json") };
                req.Headers.Authorization = new AuthenticationHeaderValue("Bearer", key);
                req.Headers.UserAgent.ParseAdd(Fbbspots.UserAgent);
                using var r2 = await http.SendAsync(req).ConfigureAwait(false);
                var t2 = await r2.Content.ReadAsStringAsync().ConfigureAwait(false);
                lines.Add(r2.IsSuccessStatusCode ? new(DiagState.Ok, L.T("Clé"), L.T("acceptée par le serveur"))
                        : r2.StatusCode is HttpStatusCode.Unauthorized or HttpStatusCode.Forbidden
                            ? new(DiagState.Fail, L.T("Clé"), L.F("refusée ({0}) : {1}", (int)r2.StatusCode, FbbspotsSender.Reason(t2)))
                            : new(DiagState.Warn, L.T("Clé"), L.F("non vérifiable (réponse {0} : {1})", (int)r2.StatusCode, FbbspotsSender.Reason(t2))));
            }
            else if (r.IsSuccessStatusCode)
            {
                using var d = JsonDocument.Parse(text);
                var root = d.RootElement;
                string S(string n) => root.TryGetProperty(n, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString()! : "";
                string call = S("callsign"), state = S("status"), last = S("last_spot");
                var detail = L.F("valide : station {0}, {1}", call, state == "" ? "?" : state)
                    + (last == "" ? L.T(", aucun spot reçu") : L.F(", dernier spot reçu par le serveur {0}", last));
                var st = state is "" or "active" ? DiagState.Ok : DiagState.Fail;
                if (st == DiagState.Ok && c.Callsign != "" && !call.Equals(c.Callsign, StringComparison.OrdinalIgnoreCase))
                {
                    st = DiagState.Warn;
                    detail += L.F(" — clé d'une autre station que {0} (Réglages › Station)", c.Callsign);
                }
                lines.Add(new(st, L.T("Clé"), detail));
            }
            else lines.Add(r.StatusCode is HttpStatusCode.Unauthorized or HttpStatusCode.Forbidden
                ? new(DiagState.Fail, L.T("Clé"), L.F("refusée ({0}) : {1}", (int)r.StatusCode, FbbspotsSender.Reason(text)))
                : new(DiagState.Warn, L.T("Clé"), L.F("non vérifiable (réponse {0} : {1})", (int)r.StatusCode, FbbspotsSender.Reason(text))));
        }
        catch (Exception e) { lines.Add(new(DiagState.Fail, L.T("Clé"), Why(e))); }
        return lines;
    }

    static async Task<HttpResponseMessage> Get(HttpClient http, string url, string? key)
    {
        using var req = new HttpRequestMessage(HttpMethod.Get, url);
        if (key is not null) req.Headers.Authorization = new AuthenticationHeaderValue("Bearer", key);
        req.Headers.UserAgent.ParseAdd(Fbbspots.UserAgent);
        return await http.SendAsync(req).ConfigureAwait(false);
    }

    // the cause in words a tester can act on
    static string Why(Exception e)
    {
        for (var x = e; x is not null; x = x.InnerException)
            if (x is AuthenticationException)
                return L.F("certificat du serveur refusé ({0}) : heure du PC fausse, Windows pas à jour, ou un antivirus qui inspecte HTTPS", x.Message);
        if (e is TaskCanceledException or TimeoutException)
            return L.T("pas de réponse en 15 s : pare-feu, antivirus ou proxy qui bloque le programme ?");
        return e.InnerException?.Message ?? e.Message;
    }

    static string Span(TimeSpan t) =>
        t.TotalSeconds < 0 ? "0 s" : t.TotalMinutes < 1 ? $"{t.Seconds} s" : t.TotalHours < 1 ? $"{(int)t.TotalMinutes} min"
        : t.TotalDays < 1 ? $"{(int)t.TotalHours} h {t.Minutes:00}" : L.F("{0} j", (int)t.TotalDays);
}
