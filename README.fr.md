# TWIST Console

*[English](README.md) · Français*

Balises pour signaux faibles en VHF/UHF/SHF : les modes de balise en
tout ou rien (OOK) **TWIST**, et **TWIST Console**, une application de
bureau pour les recevoir et les émettre, avec **OPERA** et **PI4**.

TWIST garde la modulation des balises OPERA (tout ou rien, éléments
Manchester de 0,128 s en Op05, trames non synchronisées) et remplace
seulement la chaîne de codage par un code LDPC et un CRC-16 :

| Trame | Contenu | Trame (Op05) | Code | Sensibilité (50 %) |
|---|---|---|---|---|
| OPERA (classique) | indicatif | 239 éléments, 30,6 s | 17 × [7,3] + CRC 16+3 | -21,4 dB |
| **TWIST S** | indicatif | 239 éléments, 30,6 s | LDPC (119, 44) + CRC-16 | **-22,2 dB** |
| **TWIST L** | indicatif + locator (6 caractères) | 321 éléments, 41,1 s | LDPC (160, 69) + CRC-16 | **-21,4 dB** |

(SNR de la porteuse émise dans 2500 Hz, récepteur à l'aveugle complet,
bruit blanc.) Les trames sont définies dans
[`docs/PROTOCOL.md`](docs/PROTOCOL.md) (en anglais).

Le **PI4** (PharusIgnis4, Bo Hansen OZ2M : 4-FSK, synchronisé sur la
minute) est décodé aussi, dans sa propre zone (porteuse 800 ± 150 Hz par
défaut) : 50 % décodé vers -23,6 dB, 100 % à partir de -22 dB.

## Téléchargement — Windows, Linux, macOS

TWIST Console fonctionne sur les trois systèmes ; chaque
[version publiée](https://github.com/f6fbb/TWIST/releases) contient de
quoi l'installer :

| Système | Quoi installer |
|---|---|
| **Windows** 10 / 11 (64 bits) | `TWIST-Console-<version>-setup.exe` : l'application, ses bibliothèques, Hamlib et les décodeurs, sans droits d'administrateur. Ensuite, le programme propose lui-même les nouvelles versions. |
| **macOS** 10.15 ou plus récent (Intel ; sur Apple Silicon par Rosetta 2, pas encore essayé) | `TWIST-Console-<version>-mac-intel.dmg` : glisser TWIST Console dans Applications ; la première fois, clic droit › Ouvrir (pas encore signé par Apple). |
| **Linux** (x86-64) | compilé depuis les sources en quelques minutes, voir [Compilation](#compilation) : les bibliothèques et les décodeurs par `make`, l'application par `dotnet`. |

Les décodeurs `twistd`, `operad` et `pi4d` sont fournis avec
l'installation Windows (dossier `outils`) et compilés par `make` sous
Linux et macOS. Les versions publiées jusqu'ici ne sont pas encore
signées : Windows SmartScreen demande une confirmation (Informations
complémentaires › Exécuter quand même) ; voir
[Signature du code](#signature-du-code).

## TWIST Console

L'application (C# .NET 8 + Avalonia ; Windows 10/11, Linux, macOS)
décode la carte son en continu (ou un fichier WAV), affiche un
waterfall et les spots, les publie (MQTT, base de spots fbbspots),
pilote la radio par Hamlib et émet une séquence de balise (trames
TWIST, OPERA, PI4, CW, porteuse). Son guide de l'utilisateur est
[`docs/TWIST-Console-Guide-utilisateur.pdf`](docs/TWIST-Console-Guide-utilisateur.pdf) ;
les sources sont dans [`app/`](app/README.md).

À l'installation, OPERA et PI4 sont deux composants, cochés par
défaut ; TWIST est toujours installé ; l'application ne montre que les
protocoles dont la bibliothèque est là.

## Bibliothèques et décodeurs

Le moteur est fait de quatre bibliothèques en C, les trois protocoles
indépendants les uns des autres ([`docs/LIBTWIST.md`](docs/LIBTWIST.md)) :

| Bibliothèque | En-tête | Contenu |
|---|---|---|
| `libtwcore` | `core/twcore.h` | audio (PortAudio), émission, détection de manipulation, arbitrage - aucun protocole |
| `libtwist` | `core/libtwist.h` | TWIST S et TWIST L : décodeur et codeur |
| `libopera` | `core/twcl.h` | la trame OPERA classique : décodeur (avec la recherche approfondie des indicatifs connus) et codeur |
| `libpi4` | `core/libpi4.h` | PI4 : décodeur et codeur |

et trois décodeurs en ligne de commande, un par protocole, qui lisent
un fichier WAV (audio BLU ou I/Q) :

```
twistd  [options] fichier.wav     TWIST S, TWIST L
operad  [options] fichier.wav     OPERA (trame classique)
pi4d    [options] fichier.wav     PI4
```

```
$ twistd -T 120000 balise.wav
120000 -21 34.1 1520 TWIST S : F6FBB
120000 -21 66.7 1520 TWIST L : F1HDI JN18CS
$ operad -T 120000 balise.wav
120000 -21  1.5 1520 OPERA 05 : F5ZZO
$ pi4d -T 120000 pi4.wav
120000 -11  1.4  798 : F5ZZO
```

`-h` donne les options de chacun. Détails :
[`docs/RECEIVER.md`](docs/RECEIVER.md) (en anglais).

## Contenu

| Chemin | Contenu |
|---|---|
| `core/` | le moteur (C, FFTW3) : traitement du signal commun, les trois protocoles, l'API des bibliothèques |
| `cli/` | `twistd` et `operad` (une seule source), `pi4d`, `twistenc` (éléments ou symboles d'une trame, `-w` en audio), `twistlive` (décodage continu et émission en ligne de commande) |
| `bindings/csharp/` | déclarations C# des bibliothèques, et une démonstration en console |
| `app/` | TWIST Console (C# .NET 8 + Avalonia), voir [`app/README.md`](app/README.md) |
| `installer/` | installation Windows (Inno Setup) et macOS |
| `firmware/` | codeur C autonome des trames TWIST pour les balises (ESP32, Arduino…), sans dépendance |
| `tools/` | générateur de tables (à ne pas relancer), codeur de référence indépendant en Python |
| `tests/` | tests automatiques, simulateurs, enregistrements réels |
| `docs/` | protocole, récepteur, bibliothèques, compilation sous Windows, guide de l'utilisateur |

## Compilation

Linux :

```
sudo apt install build-essential libfftw3-dev portaudio19-dev pkg-config
make                 # twistd, operad, pi4d, twistenc, twistlive + les quatre bibliothèques
make test            # tests automatiques
sudo make install    # /usr/local/bin, lib, include

sudo apt install dotnet-sdk-8.0
cd app && dotnet run --project Twist.App
```

Windows : MSYS2 UCRT64 (`pacman -S mingw-w64-ucrt-x86_64-gcc
mingw-w64-ucrt-x86_64-fftw mingw-w64-ucrt-x86_64-portaudio make`, puis
`make CC=gcc`), ou CMake + Visual Studio, voir
[`docs/WINDOWS.md`](docs/WINDOWS.md). La CI construit l'installation
de TWIST Console ; chaque version publiée la contient.

## Signature du code

Signature du code sous Windows offerte par [SignPath.io](https://about.signpath.io),
certificat de la [SignPath Foundation](https://signpath.org). *En cours
de mise en place : les versions publiées jusqu'ici ne sont pas signées.*

- **Ce qui est signé** : les installations Windows d'une version
  publiée (`TWIST-Console-<version>-setup.exe`, `-mise-a-jour.exe`) et
  nos propres programmes et bibliothèques qu'elles contiennent
  (`Twist.exe`, `twistd`, `operad`, `pi4d`, `libtwcore`, `libtwist`,
  `libopera`, `libpi4`…), compilés depuis ce dépôt par GitHub Actions
  uniquement ; les bibliothèques d'autres projets (FFTW, PortAudio,
  Hamlib, .NET, Avalonia) ne sont pas signées par nous. Détails :
  [`docs/SIGNING.md`](docs/SIGNING.md) (en anglais).
- **Contributeurs et relecteurs** : [GiPe66](https://github.com/GiPe66)
  (F6FBB).
- **Approbateurs** : [GiPe66](https://github.com/GiPe66) (F6FBB), chaque
  demande de signature approuvée à la main.
- **Confidentialité** : ce programme ne transmet aucune information à
  d'autres systèmes en réseau sans que l'utilisateur, ou la personne
  qui l'installe ou l'exploite, ne le demande expressément. Ce qu'il
  peut envoyer, chaque point activé ou non dans les Réglages :
  - la recherche de nouvelles versions : une demande de
    `update.f6fbb.org/twist/latest.json` (activée par défaut ; rien
    d'autre n'est envoyé que la demande, avec la version du programme) ;
  - le locator d'un indicatif reçu sans locator : l'indicatif, demandé
    à `spots.f6fbb.org` (activé par défaut) ;
  - les spots reçus, vers fbbspots (`spots.f6fbb.org`) ou un serveur
    MQTT : seulement si l'utilisateur les configure (désactivé par
    défaut).

## Licence et crédits

GNU GPL v3 (voir [`LICENSE`](LICENSE)). OPERA est le mode de balise
d'EA5HVK ; la chaîne de codage classique vient des sources GPL de F4GCB
et 7L1RLL. Le PI4 est le mode d'OZ2M, son décodeur d'après le JT4 de
WSJT-X (K1JT). Voir [`CREDITS.md`](CREDITS.md).
