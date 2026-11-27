# Credits

## The classic frame (OPERA)

The "classic" frame decoded and encoded by TWIST (callsign only, 239
chips) is the frame of the **OPERA** beacon mode, designed by
**Jose Alberto Nieto Ros, EA5HVK**. OPERA and its official software are
EA5HVK's work and remain his property; TWIST is not affiliated with or
endorsed by him.

TWIST contains **no code or resource from the official OPERA software**.
The classic coding chain (`core/twcenc.c`, the reference vectors of
`tests/ref_vectors.h`) is a port of the open-source implementations
published under the GNU GPL by:

- **F4GCB** (Opera coding, as distributed with rpitx),
- **7L1RLL** (Opera encoder and its test program, from which the three
  reference transmissions AA1AA, F5OEO and 7L1RLL come),
- **F5OEO** — [rpitx](https://github.com/F5OEO/rpitx), which carries
  that code.

The receiver itself (`core/`) is an original implementation.

To limit any confusion with the official software, the name "OPERA"
does not appear in the code identifiers: it is defined once, as the
display label of the compatible mode, in `core/twnames.h`
(`TW_CLASSIC_NAME`), and is used in the documentation only to say which
beacons TWIST is compatible with.

## TWIST

The TWIST S and TWIST L frames (`docs/PROTOCOL.md`), the receiver and
the tools are by **F6FBB** (Jean-Paul Roubelat) with the help of
Claude (Anthropic), 2026. Field tests: F1HDI and the first beacon
operators.

These credits are also in the application (Aide › À propos de
TWIST Console › Crédits), and this file is installed with it.

## Third-party code and libraries

| Component | Author | Licence |
|---|---|---|
| FFTW3 | M. Frigo, S. G. Johnson | GPL v2 or later |
| `core/wavread.c`, `core/iqread.c`, `core/pi4iqscan.c`, `core/pi4iqmix.c`, `cli/pi4d.c` | from the pi4d project (F6FBB) | GPL |
| PEG construction (`core/ldpc.c`) | algorithm of X.-Y. Hu, E. Eleftheriou, D.-M. Arnold (2005) | original code |
| PI4 decoder (`core/pi4*.c`, `core/pi4*.h`) | from the pi4d project (F6FBB), after JT4 in WSJT-X (K1JT) | free use, reference to OZ2M and WSJT-X kept |
| `core/fano.c`, `core/fano.h`, `core/tab.c` | Phil Karn KA9Q (1994), modifications Joe Taylor K1JT, from WSJT-X, unmodified | GPL v3 |
| PortAudio (live audio, `core/twaudio.c`) | Ross Bencina, Phil Burk and contributors | MIT-style (PortAudio licence) |

## Shipped with the application

| Component | Author | Licence |
|---|---|---|
| Hamlib (`rigctld`, `rigctl`, in `hamlib/`, with its licence files) | the Hamlib project | LGPL v2.1 (library), GPL v2 (programs) |
| Avalonia (user interface) | AvaloniaUI OÜ and contributors | MIT |
| CommunityToolkit.Mvvm | .NET Foundation and contributors | MIT |
| MQTTnet | .NET Foundation and contributors | MIT |
| Inter (font, via Avalonia.Fonts.Inter) | Rasmus Andersson | SIL Open Font License 1.1 |
| .NET runtime | Microsoft, .NET Foundation | MIT |

Used to build only (not shipped): Inno Setup (Jordan Russell, Martijn
Laan; the Windows installers), xUnit (tests), docx for Node.js (the
user's guide).

The PI4 protocol (PharusIgnis4) is specified by **Bo Hansen, OZ2M**:
<https://rudius.net/oz2m/ngnb/pi4_.htm>. Its convolutional code and
decoding come from JT4, by **Joe Taylor, K1JT**, and the WSJT
development team.

## Data of the callsign checks

`app/Twist.App/Assets/callcheck.json` (made by
`tools/callcheck/gen_callcheck.py`, read by `Services/CallCheck.cs`):

| Data | Source | Licence |
|---|---|---|
| Prefixes and DXCC entities | Amateur Radio Country Files by **Jim Reisert AD1C**, [country-files.com](https://www.country-files.com/), as packaged in [@ham2k/lib-country-files](https://www.npmjs.com/package/@ham2k/lib-country-files) (Sebastian Delmont) | free use of the files; package MIT |
| Country outlines | [Natural Earth](https://www.naturalearthdata.com/) 4.1.0, as TopoJSON in [world-atlas](https://github.com/topojson/world-atlas) 2.0.2 (Mike Bostock) | public domain; package ISC |

References used: D. Estévez, *Testing Opera sensitivity with GNU Radio*
(2016); [sigidwiki — OPERA Beacon and Data](https://www.sigidwiki.com/wiki/OPERA_Beacon_and_Data).
