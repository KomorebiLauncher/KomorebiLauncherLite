; Installer for Komorebi Lite. It is compiled by tools/package.ps1, which passes the version and the
; directory holding the frozen launcher; Inno Setup is the only tool that has to be installed to make
; it, and CI installs it.
;
; Per-user install on purpose: a launcher does not belong in Program Files, and {autopf} resolves to
; %LOCALAPPDATA%\Programs for a lowest-privileges install, so no UAC prompt appears.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\dist\stage"
#endif
#ifndef OutDir
  #define OutDir "..\dist"
#endif

#define AppName "Komorebi Lite"
#define AppExe "komorebi-lite.exe"
#define AppUrl "https://github.com/KomorebiLauncher/KomorebiLauncherLite"
#define SourceDir AddBackslash(SourcePath) + ".."

[Setup]
AppId={{7B3E1C4A-59D2-4E7F-9A61-2C8D5F0B6E14}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=KomorebiLauncher contributors
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
VersionInfoVersion={#AppVersion}
VersionInfoCompany=KomorebiLauncher contributors
VersionInfoDescription={#AppName} {#AppVersion} setup
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
VersionInfoCopyright=Copyright (C) 2025-2026 KomorebiLauncher contributors. GPL-3.0-only.
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile={#SourceDir}\LICENSE
OutputDir={#OutDir}
OutputBaseFilename=KomorebiLite-Setup-{#AppVersion}
SetupIconFile={#SourceDir}\resources\komorebi-lite.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=yes
RestartApplications=no
MinVersion=10.0
ShowLanguageDialog=auto

[Languages]
Name: "zh"; MessagesFile: "{#SourceDir}\tools\inno\ChineseSimplified.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The frozen launcher: the executable, the Qt runtime windeployqt copied, the theme and the OptiFine
; helper. Nothing is written outside {app} by the setup, and the player's data stays in
; %APPDATA%\KomorebiLauncherLite, where an uninstall does not reach.
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Logs the launcher drops beside itself; everything else in {app} is Inno's own bookkeeping.
Type: files; Name: "{app}\*.log"
