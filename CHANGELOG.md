# Changelog

TWIST Console and its libraries have one version number. Each version
has its section below, `## [version] - date`: the CI takes it as the
text of the Release and of the update offered by the program.

## [1.0.0-rc.1] - 2026-11-27

The first public version of TWIST Console, after the private test
versions of TWIST-OOK (0.x).

### TWIST Console (the application)
- Reception and transmission of the TWIST S and TWIST L beacon frames,
  of OPERA (Op05) and of PI4: continuous decoding of the sound card or
  of a WAV file, waterfall, spots list and CSV journal, publication
  (MQTT, fbbspots), radio through Hamlib, beacon sequences (frames, CW,
  carrier, pauses) on their second, Tune, recordings, update check.
- OPERA and PI4 are optional components of the installer, checked by
  default; the protocols whose library is not installed are not shown.
- Formerly TWIST-OOK: installed over it (same settings, same folder),
  its shortcuts replaced.
- Réglages: an **Appliquer** button saves and applies the settings
  without closing the window (to try one audio input after another).

### Libraries
- The engine is four libraries: `libtwcore` (audio, transmission,
  keying detector, arbitration; no protocol), `libtwist` (TWIST S,
  TWIST L), `libopera` (the classic frame) and `libpi4` (PI4). The
  three protocol libraries depend on no other (`docs/LIBTWIST.md`).

### Decoders
- `twistd` (TWIST S, TWIST L), `operad` (OPERA, with the deep search
  for known callsigns) and `pi4d` (PI4: the program of the pi4d
  project, same options and output), installed with TWIST Console in
  its `outils` folder.
