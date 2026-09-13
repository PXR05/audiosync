#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\build\release"
#endif
#ifndef OutputPath
  #define OutputPath "..\..\dist"
#endif

[Setup]
AppId={{8DFA1442-A417-446B-A0F4-B5B0F68319B4}
AppName=AudioSync
AppVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\AudioSync
DefaultGroupName=AudioSync
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputPath}
OutputBaseFilename=AudioSync-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\audiosync.exe
AppMutex=AudioSyncSingleInstance
CloseApplications=no
RestartApplications=no
SetupLogging=yes

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#SourceDir}\audiosync.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\docs\*.md"; DestDir: "{app}\docs"; Flags: ignoreversion

[Icons]
Name: "{group}\AudioSync"; Filename: "{app}\audiosync.exe"; Parameters: "watch"
Name: "{autodesktop}\AudioSync"; Filename: "{app}\audiosync.exe"; Parameters: "watch"; Tasks: desktopicon

[Run]
Filename: "{app}\audiosync.exe"; Parameters: "watch"; Description: "Start AudioSync in the tray"; Flags: nowait postinstall skipifsilent unchecked

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Command: String;
begin
  if CurUninstallStep = usUninstall then
    if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'AudioSync', Command) then
      if (CompareText(Command, '"' + ExpandConstant('{app}\audiosync.exe') + '"') = 0) or
         (CompareText(Command, '"' + ExpandConstant('{app}\audiosync.exe') + '" watch') = 0) then
        RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'AudioSync');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Command: String;
begin
  if CurStep = ssPostInstall then
    if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'AudioSync', Command) then
      if CompareText(Command, '"' + ExpandConstant('{app}\audiosync.exe') + '"') = 0 then
        RegWriteStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'AudioSync',
                            '"' + ExpandConstant('{app}\audiosync.exe') + '" watch');
end;
