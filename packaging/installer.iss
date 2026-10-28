; Inno Setup script for the Trinity AI Windows installer.
;
; Input: the assembled (and already smoke-verified) portable directory,
; dist/TrinityAI-<ver>-win64/. The same layout ships as the portable zip,
; so installer and zip are byte-identical in contents by construction.
;
; Installs per-user to %LOCALAPPDATA%\Programs\TrinityAI (no admin),
; Start Menu shortcut "Trinity AI" that runs the bundled launcher.

; Cross-version compile override: ISCC /DVersion=1.2.3
#ifndef Version
  #define Version "1.0.0"
#endif

[Setup]
AppId={{6E9C2B7A-4B3D-4E8F-9A15-C1D2F3A4B5C6}
AppName=Trinity AI
AppVersion={#Version}
AppPublisher=Trinity AI
VersionInfoCompany=Trinity AI
DefaultDirName={localappdata}\Programs\TrinityAI
DefaultGroupName=Trinity AI
; No admin rights required; per-user install.
PrivilegesRequired=lowest
OutputDir=dist
OutputBaseFilename=TrinityAI-{#Version}-win64-setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\native\Trinity.exe

[Files]
Source: "dist\TrinityAI-{#Version}-win64\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs uninsrestartdelete

[Icons]
Name: "{group}\Trinity AI"; Filename: "{app}\Trinity.bat"; WorkingDir: "{app}"; Comment: "Trinity AI - Engineering Operating System"

[Run]
Filename: "{app}\Trinity.bat"; Description: "Launch Trinity AI"; Flags: nowait postinstall skipifsilent

[UninstallRun]
