; TWIST Console installer for Windows (Inno Setup 6).
;
; Built by the CI (windows job) from the published application:
;   iscc /DAppVersion=1.0.0-rc.1 /DNumVersion=1.0.0 /DSrc=<dist> installer\twist-console.iss
; with in <dist>:
;   app\        Twist.exe (self-contained), libtwcore.dll, libtwist.dll,
;               libopera.dll, libpi4.dll and their DLLs, hamlib\
;   outils\     twistd, operad, pi4d and their DLLs
;   TESTEURS.txt  docs/TESTEURS.md in UTF-8 with a BOM (accents shown right)
;
; Components: TWIST Console with TWIST S and TWIST L, always; OPERA
; (libopera, operad) and PI4 (libpi4, pi4d), checked by default - the
; program shows only the protocols whose library is installed. Reinstalled
; without one, its files are removed.
;
; Per-user installation by default (no administrator rights), or for all
; users if chosen. Settings (%APPDATA%\TWIST) and the CSV journal are
; kept on update and on uninstall. Over TWIST-OOK (same AppId): installed
; in its folder, its shortcuts replaced.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
; the version as Windows wants it in the file properties: numbers only
#ifndef NumVersion
  #define NumVersion "0.0.0"
#endif
#ifndef Src
  #define Src "..\dist"
#endif

[Setup]
AppId={{6B0E2C1A-7F0B-4E59-9F3D-2A7C1D0B7E51}
AppName=TWIST Console
AppVersion={#AppVersion}
AppVerName=TWIST Console {#AppVersion}
AppPublisher=F6FBB
AppPublisherURL=https://github.com/f6fbb/TWIST
VersionInfoVersion={#NumVersion}
DefaultDirName={autopf}\TWIST Console
DefaultGroupName=TWIST Console
DisableProgramGroupPage=yes
; the shortcuts in "TWIST Console", not in the group of TWIST-OOK
UsePreviousGroup=no
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=..\LICENSE
InfoAfterFile={#Src}\TESTEURS.txt
SetupIconFile=..\app\Twist.App\Assets\twist.ico
UninstallDisplayIcon={app}\Twist.exe
OutputDir={#Src}\setup
OutputBaseFilename=TWIST-Console-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Languages]
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "{cm:FullInstall}"
Name: "custom"; Description: "{cm:CustomInstall}"; Flags: iscustom

[Components]
Name: "main"; Description: "TWIST Console (TWIST S, TWIST L)"; Types: full custom; Flags: fixed
Name: "opera"; Description: "OPERA"; Types: full
Name: "pi4"; Description: "PI4"; Types: full

[CustomMessages]
fr.FullInstall=Installation complète (TWIST, OPERA, PI4)
en.FullInstall=Full installation (TWIST, OPERA, PI4)
fr.CustomInstall=Installation personnalisée
en.CustomInstall=Custom installation

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#Src}\app\*"; DestDir: "{app}"; Excludes: "libopera.dll,libpi4.dll"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: main
Source: "{#Src}\app\libopera.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: opera
Source: "{#Src}\app\libpi4.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: pi4
Source: "{#Src}\outils\*"; DestDir: "{app}\outils"; Excludes: "operad.exe,pi4d.exe"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: main
Source: "{#Src}\outils\operad.exe"; DestDir: "{app}\outils"; Flags: ignoreversion; Components: opera
Source: "{#Src}\outils\pi4d.exe"; DestDir: "{app}\outils"; Flags: ignoreversion; Components: pi4
Source: "{#Src}\TESTEURS.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\CREDITS.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\TWIST-Console-Guide-utilisateur.pdf"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\TWIST-Console-Guide-utilisateur.docx"; DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
; a protocol not chosen this time: its library and its decoder go
Type: files; Name: "{app}\libopera.dll"; Components: not opera
Type: files; Name: "{app}\outils\operad.exe"; Components: not opera
Type: files; Name: "{app}\libpi4.dll"; Components: not pi4
Type: files; Name: "{app}\outils\pi4d.exe"; Components: not pi4
; over TWIST-OOK: the engine of one library, its tools, guide and shortcuts
Type: files; Name: "{app}\outils\twistenc.exe"
Type: files; Name: "{app}\outils\twistlive.exe"
Type: files; Name: "{app}\outils\libtwist.dll"
Type: files; Name: "{app}\TWIST-OOK-Guide-utilisateur.*"
Type: filesandordirs; Name: "{autoprograms}\TWIST-OOK"
Type: files; Name: "{autodesktop}\TWIST-OOK.lnk"

[Icons]
Name: "{group}\TWIST Console"; Filename: "{app}\Twist.exe"
Name: "{group}\TWIST Console (affichage logiciel)"; Filename: "{app}\Twist.exe"; Parameters: "--software"; Comment: "Si la fenêtre reste vide ou noire (pilote graphique)"
Name: "{group}\Guide de l'utilisateur"; Filename: "{app}\TWIST-Console-Guide-utilisateur.pdf"
Name: "{group}\Guide des testeurs"; Filename: "{app}\TESTEURS.txt"
Name: "{group}\{cm:UninstallProgram,TWIST Console}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\TWIST Console"; Filename: "{app}\Twist.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Twist.exe"; Description: "{cm:LaunchProgram,TWIST Console}"; Flags: nowait postinstall skipifsilent
; started by the program itself (Aide › mise à jour: /SILENT /RELAUNCH): TWIST Console again when done
Filename: "{app}\Twist.exe"; Flags: nowait runasoriginaluser; Check: Relaunch

[Code]
// the program asked for its own update (Services/Updates.cs): start it again
function Relaunch: Boolean;
begin
  Result := Pos('/RELAUNCH', UpperCase(GetCmdTail)) > 0;
end;
