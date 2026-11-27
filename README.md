# TWIST Console

Weak-signal beacons for VHF/UHF/SHF: the **TWIST** on/off-keyed (OOK)
beacon modes, and **TWIST Console**, a desktop application to receive
and transmit them, together with **OPERA** and **PI4**.

TWIST keeps the modulation of the OPERA beacons (on/off keying,
Manchester chips of 0.128 s in Op05, asynchronous frames) and replaces
only the coding chain with an LDPC code and a CRC-16:

| Frame | Content | Frame (Op05) | Code | Sensitivity (50 %) |
|---|---|---|---|---|
| OPERA (classic) | callsign | 239 chips, 30.6 s | 17 × [7,3] + CRC 16+3 | -21.4 dB |
| **TWIST S** | callsign | 239 chips, 30.6 s | LDPC (119, 44) + CRC-16 | **-22.2 dB** |
| **TWIST L** | callsign + locator (6 chars) | 321 chips, 41.1 s | LDPC (160, 69) + CRC-16 | **-21.4 dB** |

(carrier-on SNR in 2500 Hz, full blind receiver, white noise.) The
frames are defined in [`docs/PROTOCOL.md`](docs/PROTOCOL.md).

**PI4** (PharusIgnis4, Bo Hansen OZ2M: 4-FSK, synchronous on the
minute) is decoded too, in its own zone (carrier 800 ± 150 Hz by
default): 50 % decoded at about -23.6 dB, 100 % from -22 dB.

## TWIST Console

The application (C# .NET 8 + Avalonia; Windows 10/11, Linux, macOS)
decodes the sound card continuously (or a WAV file), shows a waterfall
and the spots, publishes them (MQTT, the spot database fbbspots),
drives the radio through Hamlib and transmits a beacon sequence (TWIST,
OPERA, PI4 frames, CW, carrier). Its user's guide (French) is
[`docs/TWIST-Console-Guide-utilisateur.pdf`](docs/TWIST-Console-Guide-utilisateur.pdf);
the sources are in [`app/`](app/README.md).

At installation, OPERA and PI4 are two components, checked by
default: TWIST is always installed; the application shows only the
protocols whose library is there.

## Libraries and decoders

The engine is four C libraries, the three protocols independent of each
other ([`docs/LIBTWIST.md`](docs/LIBTWIST.md)):

| Library | Header | Content |
|---|---|---|
| `libtwcore` | `core/twcore.h` | audio (PortAudio), transmission, keying detector, arbitration - no protocol |
| `libtwist` | `core/libtwist.h` | TWIST S and TWIST L: decoder and encoder |
| `libopera` | `core/twcl.h` | the classic OPERA frame: decoder (with the deep search for known callsigns) and encoder |
| `libpi4` | `core/libpi4.h` | PI4: decoder and encoder |

and three command-line decoders, one per protocol, reading a WAV file
(SSB audio or I/Q):

```
twistd  [options] file.wav     TWIST S, TWIST L
operad  [options] file.wav     OPERA (classic frame)
pi4d    [options] file.wav     PI4
```

```
$ twistd -T 120000 beacon.wav
120000 -21 34.1 1520 TWIST S : F6FBB
120000 -21 66.7 1520 TWIST L : F1HDI JN18CS
$ operad -T 120000 beacon.wav
120000 -21  1.5 1520 OPERA 05 : F5ZZO
$ pi4d -T 120000 pi4.wav
120000 -11  1.4  798 : F5ZZO
```

`-h` lists the options of each. Details: [`docs/RECEIVER.md`](docs/RECEIVER.md).

## Contents

| Path | Content |
|---|---|
| `core/` | the engine (C, FFTW3): common signal processing, the three protocols, the libraries' API |
| `cli/` | `twistd` and `operad` (one source), `pi4d`, `twistenc` (frame chips or symbols, `-w` audio), `twistlive` (continuous decoder and transmitter on the command line) |
| `bindings/csharp/` | C# declarations of the libraries, and a console demo |
| `app/` | TWIST Console (C# .NET 8 + Avalonia), see [`app/README.md`](app/README.md) |
| `installer/` | Windows (Inno Setup) and macOS packaging |
| `firmware/` | stand-alone C encoder of the TWIST frames for beacons (ESP32, Arduino…), no dependency |
| `tools/` | table generator (do not re-run), an independent Python reference encoder |
| `tests/` | self-checking test suite, simulators, real off-air recordings |
| `docs/` | protocol, receiver, libraries, Windows build, user's guide |

## Build

Linux:

```
sudo apt install build-essential libfftw3-dev portaudio19-dev pkg-config
make                 # twistd, operad, pi4d, twistenc, twistlive + the four libraries
make test            # self-checking suite
sudo make install    # /usr/local/bin, lib, include

sudo apt install dotnet-sdk-8.0
cd app && dotnet run --project Twist.App
```

Windows: MSYS2 UCRT64 (`pacman -S mingw-w64-ucrt-x86_64-gcc
mingw-w64-ucrt-x86_64-fftw mingw-w64-ucrt-x86_64-portaudio make`, then
`make CC=gcc`), or CMake + Visual Studio, see
[`docs/WINDOWS.md`](docs/WINDOWS.md). The CI builds the installer of
TWIST Console; each Release carries it.

## Licence and credits

GNU GPL v3 (see [`LICENSE`](LICENSE)). OPERA is the beacon mode of
EA5HVK; the classic coding chain comes from the GPL sources of F4GCB
and 7L1RLL. PI4 is the mode of OZ2M, its decoder after JT4 of WSJT-X
(K1JT). See [`CREDITS.md`](CREDITS.md).

---

### En bref (français)

TWIST est un ensemble de modes de balise OOK pour signaux faibles, qui
garde la modulation des balises OPERA et remplace le codage par un code
LDPC : TWIST S (indicatif, +0,8 dB par rapport à OPERA) et TWIST L
(indicatif + locator, même sensibilité qu'OPERA). **TWIST Console**
reçoit et émet TWIST, OPERA et PI4 ; OPERA et PI4 sont des composants
au choix à l'installation. Les décodeurs en ligne de commande sont
`twistd` (TWIST), `operad` (OPERA) et `pi4d` (PI4). Le guide de
l'utilisateur est dans `docs/`.
