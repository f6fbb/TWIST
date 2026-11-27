# TWIST Console — desktop application

C# .NET 8 + Avalonia 11 (MVVM, CommunityToolkit.Mvvm), over the
libraries of the engine (`libtwcore`, `libtwist`, and `libopera`,
`libpi4` when installed: `docs/LIBTWIST.md`) through the C# bindings of
`bindings/csharp/LibTwist.cs`. Specification:
[`docs/SPECIFICATION.md`](../docs/SPECIFICATION.md).

| Project | Content |
|---|---|
| `Twist.App` | the application (`Twist.exe`) |
| `Twist.Screenshots` | renders every window to PNG without a screen (Avalonia headless) — used by the CI |

## Build and run

```
make                                   # repository root: the engine library
cd app
dotnet run --project Twist.App
```

or open `app/Twist.sln` in Visual Studio 2022 (or Rider). The library
built by `make` is copied next to the program; without it the
application starts and says so in its status bar.

The Windows CI artifact **twist-windows-x64** carries a ready-to-run
self-contained build in its `app/` folder (`Twist.exe`, no .NET
installation needed). The Linux job publishes **twist-app-screenshots**.

## Structure

| Folder | Content |
|---|---|
| `Views/` | windows in XAML: main window, Balise, Réglages, Outils |
| `ViewModels/` | their data and commands (MVVM): what a window shows, without drawing code |
| `Controls/` | drawn controls: level meter, waterfall, chip strip |

## Windows installer

The CI builds `TWIST-Console-<version>-setup.exe` (artifact
`twist-console-setup`) with Inno Setup from `installer/twist-console.iss`; the
guide for the testers is `docs/TESTEURS.md`. The program is not signed:
SmartScreen asks for confirmation ("Informations complémentaires" ›
"Exécuter quand même").

## Optional protocols

OPERA (`libopera`) and PI4 (`libpi4`) are components of the installer,
checked by default. `ViewModels/Engine.cs` says which libraries are
there (`Engine.HasClassic`, `Engine.HasPi4`); the protocols whose
library is absent are hidden from the menus, the toolbar and the
settings, and left out of the decoding and the transmit sequences.
