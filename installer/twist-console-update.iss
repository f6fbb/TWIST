; TWIST Console update for Windows (Inno Setup 6): only what changes from one
; version to the next - the application (Twist.dll...), the libraries
; (libtwcore.dll, libtwist.dll, and libopera.dll, libpi4.dll where they
; are installed: the components chosen at installation stay as they
; were) and their DLLs, the decoders, the testers' guide and the user's guide
; (PDF; the .docx, to edit, comes with the full installer) - a few MB instead
; of the whole installer (the .NET runtime, Avalonia and Hamlib stay as
; they are). For a tester on a slow or metered connection (4G).
;
; Built by the CI (windows job) next to the full installer:
;   iscc /DAppVersion=1.0.1 /DNumVersion=1.0.1 /DBaseVersion=1.0.0 /DSrc=<dist> installer\twist-console-update.iss
; with in <dist>\update the files it carries. BaseVersion: the oldest
; release with the same NuGet packages (the CI checks Twist.App.csproj
; release after release); an older installation is refused - it needs
; the full installer. Same AppId as the full installer: installed in the
; same folder, with the same rights, the version shown in Windows'
; list of programs is updated, the uninstaller removes everything.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef NumVersion
  #define NumVersion "0.0.0"
#endif
#ifndef BaseVersion
  #define BaseVersion AppVersion
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
DisableDirPage=yes
DisableProgramGroupPage=yes
UsePreviousAppDir=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
UsePreviousPrivileges=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
SetupIconFile=..\app\Twist.App\Assets\twist.ico
UninstallDisplayIcon={app}\Twist.exe
OutputDir={#Src}\setup
OutputBaseFilename=TWIST-Console-{#AppVersion}-mise-a-jour
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Languages]
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#Src}\update\*"; DestDir: "{app}"; Excludes: "libopera.dll,libpi4.dll,operad.exe,pi4d.exe"; Flags: ignoreversion recursesubdirs createallsubdirs
; the optional protocols: updated only where they are installed
Source: "{#Src}\update\libopera.dll"; DestDir: "{app}"; Flags: ignoreversion onlyifdestfileexists skipifsourcedoesntexist
Source: "{#Src}\update\libpi4.dll"; DestDir: "{app}"; Flags: ignoreversion onlyifdestfileexists skipifsourcedoesntexist
Source: "{#Src}\update\outils\operad.exe"; DestDir: "{app}\outils"; Flags: ignoreversion onlyifdestfileexists skipifsourcedoesntexist
Source: "{#Src}\update\outils\pi4d.exe"; DestDir: "{app}\outils"; Flags: ignoreversion onlyifdestfileexists skipifsourcedoesntexist

[Run]
Filename: "{app}\Twist.exe"; Description: "{cm:LaunchProgram,TWIST Console}"; Flags: nowait postinstall skipifsilent
; started by the program itself (Aide › mise à jour: /SILENT /RELAUNCH): TWIST Console again when done
Filename: "{app}\Twist.exe"; Flags: nowait runasoriginaluser; Check: Relaunch

[Code]
const
  UninstKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{6B0E2C1A-7F0B-4E59-9F3D-2A7C1D0B7E51}_is1';

// the version installed now ('' if none), per user or for all users
function InstalledVersion(): String;
begin
  Result := '';
  if not RegQueryStringValue(HKCU, UninstKey, 'DisplayVersion', Result) then
    if not RegQueryStringValue(HKLM64, UninstKey, 'DisplayVersion', Result) then
      RegQueryStringValue(HKLM32, UninstKey, 'DisplayVersion', Result);
end;

// "0.19.0" -> 19000 (major * 1000000 + minor * 1000 + patch)
function VersionNumber(V: String): Integer;
var
  I, Part, N: Integer;
begin
  Result := 0; Part := 0; N := 0;
  for I := 1 to Length(V) do
    if (V[I] >= '0') and (V[I] <= '9') then
      N := N * 10 + Ord(V[I]) - Ord('0')
    else if V[I] = '.' then
    begin
      Result := Result * 1000 + N; N := 0; Part := Part + 1;
    end
    else Break;
  Result := Result * 1000 + N;
  while Part < 2 do begin Result := Result * 1000; Part := Part + 1; end;
end;

function InitializeSetup(): Boolean;
var
  V: String;
begin
  Result := True;
  V := InstalledVersion();
  if V = '' then
  begin
    SuppressibleMsgBox('TWIST Console n''est pas installé sur cet ordinateur : installez d''abord TWIST-Console-{#AppVersion}-setup.exe (installation complète).' + #13#10#13#10 +
      'TWIST Console is not installed on this computer: install TWIST-Console-{#AppVersion}-setup.exe first (full installer).', mbError, MB_OK, IDOK);
    Result := False;
  end
  else if VersionNumber(V) < VersionNumber('{#BaseVersion}') then
  begin
    SuppressibleMsgBox('Cette mise à jour s''applique à TWIST Console {#BaseVersion} ou plus récent ; la version installée est ' + V + '. Installez TWIST-Console-{#AppVersion}-setup.exe (installation complète).' + #13#10#13#10 +
      'This update applies to TWIST Console {#BaseVersion} or newer; ' + V + ' is installed. Install TWIST-Console-{#AppVersion}-setup.exe (full installer).', mbError, MB_OK, IDOK);
    Result := False;
  end;
end;

// the program asked for its own update (Services/Updates.cs): start it again
function Relaunch: Boolean;
begin
  Result := Pos('/RELAUNCH', UpperCase(GetCmdTail)) > 0;
end;
