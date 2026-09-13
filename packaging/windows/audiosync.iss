#ifndef AppVersion
  #define AppVersion "0.1.1"
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
ChangesEnvironment=yes

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
function HasPathEntry(const Paths, Entry: String): Boolean;
begin
  Result := Pos(';' + Uppercase(Entry) + ';', ';' + Uppercase(Paths) + ';') > 0;
end;

function RemovePathEntry(const Paths, Entry: String): String;
var
  Wrapped, Needle: String;
  Position: Integer;
begin
  Wrapped := ';' + Paths + ';';
  Needle := ';' + Entry + ';';
  Position := Pos(Uppercase(Needle), Uppercase(Wrapped));
  if Position > 0 then
    Delete(Wrapped, Position + 1, Length(Entry) + 1);
  if Length(Wrapped) > 1 then
    Result := Copy(Wrapped, 2, Length(Wrapped) - 2)
  else
    Result := '';
end;

procedure UpdatePath(Add: Boolean);
var
  AppDir, Paths, Updated: String;
begin
  AppDir := ExpandConstant('{app}');
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', Paths) then
    Paths := '';
  Updated := Paths;
  if Add and not HasPathEntry(Paths, AppDir) then begin
    if (Paths <> '') and (Paths[Length(Paths)] <> ';') then
      Updated := Paths + ';' + AppDir
    else
      Updated := Paths + AppDir;
  end else if not Add then
    Updated := RemovePathEntry(Paths, AppDir);
  if Updated <> Paths then
    if Updated = '' then
      RegDeleteValue(HKCU, 'Environment', 'Path')
    else
      RegWriteExpandStringValue(HKCU, 'Environment', 'Path', Updated);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Command: String;
begin
  if CurUninstallStep = usUninstall then begin
    UpdatePath(False);
    if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'AudioSync', Command) then
      if (CompareText(Command, '"' + ExpandConstant('{app}\audiosync.exe') + '"') = 0) or
         (CompareText(Command, '"' + ExpandConstant('{app}\audiosync.exe') + '" watch') = 0) then
        RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'AudioSync');
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Command: String;
begin
  if CurStep = ssPostInstall then begin
    UpdatePath(True);
    if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'AudioSync', Command) then
      if CompareText(Command, '"' + ExpandConstant('{app}\audiosync.exe') + '"') = 0 then
        RegWriteStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'AudioSync',
                            '"' + ExpandConstant('{app}\audiosync.exe') + '" watch');
  end;
end;
