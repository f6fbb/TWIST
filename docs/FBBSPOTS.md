# fbbspots — the protocol of spots.f6fbb.org

What TWIST Console asks of spots.f6fbb.org, over HTTPS, JSON in and out.
Every request carries `User-Agent: TWIST-Console/<version>`. The client side
is `app/Twist.App/Services/Fbbspots.cs`, `Locators.cs` and
`FbbspotsDiag.cs`.

| Route | Key | Purpose |
|---|---|---|
| `POST /register` | — | ask for a station key (sent by email) |
| `POST /spot` | `Authorization: Bearer fbb_…` | send spots |
| `GET /locator?call=…` | — | the locator of a callsign |
| `GET /ping` | — | **new**: is the service there, its version, its time |
| `GET /check` | `Authorization: Bearer fbb_…` | **new**: who this key is, its state |

## Existing routes

`POST /register` `{"callsign","locator","email","lang"}` →
200 `{"message"}` sent, 202 `{"message"}` waiting for the administrator,
4xx `{"message"}` why not.

`POST /spot` — one spot or a list (100 at most), each in the JSON of
spyfbbclient (`Mqtt.Json`), plus `"software": "TWIST Console 0.27.1"` (the
program that made it: `spots.software`, shown on the Reporter cell of
spots.f6fbb.org; printable ASCII, 48 characters at most; the server falls
back on the `User-Agent` without it, and keeps that per station,
`stations.program`) →
200 `{"accepted":n,"rejected":[{"index":i,"reason":"…"}]}`,
401 `{"error":"unknown key"}`, 403 `{"error":"station disabled"}` or
`{"error":"waiting for validation"}`.
**An empty list `[]` must answer 200 `{"accepted":0,"rejected":[]}`**
after the key is checked: until `/check` exists, the diagnostic uses it
to test a key without recording anything.

`GET /locator?call=F5ZZO` → 200 `{"locator":"JN12AX","approximate":false}`,
or `{"locator":""}` when unknown.

## New: `GET /ping`

No key. Answers at once, without touching the database (or with the
lightest query), so that it says whether the web server and PHP answer:

```
200 {"service":"fbbspots","version":"1.3","time":"2026-11-11T17:00:00Z"}
```

`version`: free text (the server's own version, shown in the
diagnostic). The HTTP `Date` header (nginx sends it) is what the
program compares with the PC's clock; `time` is there for a human.

## New: `GET /check`

With the station's key, as `/spot`. Records nothing.

```
200 {"callsign":"F6FBB","locator":"JN12AX","status":"active",
     "last_spot":"2026-11-11T16:58:02Z","spots_24h":42}
401 {"error":"unknown key (replaced by a newer one?)"}
403 {"error":"station disabled"}            (or "waiting for validation")
```

- `status`: `active`, `pending` (waiting for validation) or `disabled`.
  Anything but `active` is shown as a problem.
- `last_spot`: when the server last received a spot with this key (ISO
  8601 UTC, or `""` if never). This is what answers "my spots do not
  arrive any more": the program's own last success against the
  server's last spot.
- `spots_24h`: optional.

## Server side: a sketch (PHP)

To adapt: the table and column names below (`stations`: `api_key`,
`callsign`, `locator`, `status`; `spots`: `api_key` or `reporter`,
`received`) are guesses, not the server's.

```php
<?php // ping.php   (nginx: location = /ping { … ping.php })
header('Content-Type: application/json');
echo json_encode(['service' => 'fbbspots', 'version' => '1.3',
                  'time' => gmdate('Y-m-d\TH:i:s\Z')]);
```

```php
<?php // check.php  (nginx: location = /check { … check.php })
header('Content-Type: application/json');
require 'db.php';                                  // the PDO $db of spot.php
$auth = $_SERVER['HTTP_AUTHORIZATION'] ?? '';
if (!preg_match('/^Bearer (fbb_[0-9a-f]{40})$/', $auth, $m)) {
    http_response_code(401); echo json_encode(['error' => 'no key']); exit;
}
$st = $db->prepare('SELECT callsign, locator, status FROM stations WHERE api_key = ?');
$st->execute([$m[1]]);
$s = $st->fetch(PDO::FETCH_ASSOC);
if (!$s) { http_response_code(401); echo json_encode(['error' => 'unknown key']); exit; }
if ($s['status'] !== 'active') {
    http_response_code(403);
    echo json_encode(['error' => $s['status'] === 'pending' ? 'waiting for validation' : 'station disabled']); exit;
}
$q = $db->prepare('SELECT MAX(received) AS last, SUM(received > NOW() - INTERVAL 1 DAY) AS n
                   FROM spots WHERE api_key = ?');
$q->execute([$m[1]]);
$r = $q->fetch(PDO::FETCH_ASSOC);
echo json_encode(['callsign' => $s['callsign'], 'locator' => $s['locator'], 'status' => 'active',
                  'last_spot' => $r['last'] ? gmdate('Y-m-d\TH:i:s\Z', strtotime($r['last'] . ' UTC')) : '',
                  'spots_24h' => (int)$r['n']]);
```

nginx passes `Authorization` to PHP-FPM by default (`fastcgi_params`);
if `$_SERVER['HTTP_AUTHORIZATION']` is empty, add
`fastcgi_param HTTP_AUTHORIZATION $http_authorization;`.

## What the program does with them

Aide › Diagnostic fbbspots (and Réglages › Rapports › Diagnostic…):
settings, radio frequency, the sender's state, DNS, `GET /ping` (else
`GET /`), the clock against `Date`, `GET /check` (else `POST /spot []`).
Each step ✔ ⚠ ✘ with what it found; Copier for a message to the
administrator; the lines also go to the journal (and so to the testers'
report).

The sender: a refused key (401, 403) pauses the sending, the spots kept,
tried again every 10 minutes (it used to stop until the settings
changed); the server unreachable: tried again every 30 s.
