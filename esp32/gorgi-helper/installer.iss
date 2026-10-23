; Windows installer for Gorgi Helper (built by GitHub Actions with Inno Setup 6).
; Installs for the current user only, so updates never need an admin password.
;   iscc /DAppVersion=3.4 installer.iss

#ifndef AppVersion
  #define AppVersion "0.0"
#endif

[Setup]
AppId={{7C5E2F41-6B0D-4E3A-9B7E-2D9C4A1F8E63}
AppName=Gorgi Helper
AppVersion={#AppVersion}
AppPublisher=Gorgi
AppPublisherURL=https://github.com/SharonPerets/flor
DefaultDirName={localappdata}\Programs\Gorgi Helper
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=lowest
OutputDir=installer-out
OutputBaseFilename=GorgiHelper-Setup-{#AppVersion}
SetupIconFile=gorgi.ico
UninstallDisplayIcon={app}\GorgiHelper.exe
UninstallDisplayName=Gorgi Helper
Compression=lzma2/normal
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
WizardStyle=modern

[Tasks]
Name: "autostart"; Description: "Start Gorgi Helper when Windows starts"

[InstallDelete]
; remove the previous version's libraries so old and new files never mix
Type: filesandordirs; Name: "{app}\_internal"

[Files]
Source: "dist\GorgiHelper\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\Gorgi Helper"; Filename: "{app}\GorgiHelper.exe"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Gorgi Helper"; ValueData: """{app}\GorgiHelper.exe"""; Tasks: autostart; Flags: uninsdeletevalue

[Run]
; also runs after a quiet update, so the helper comes back by itself
Filename: "{app}\GorgiHelper.exe"; Description: "Start Gorgi Helper now"; Flags: nowait postinstall

[Code]
// Close any running Gorgi Helper first: an old helper that didn't quit properly (4.1 - 4.3)
// would otherwise keep its files busy and stop the new one from starting.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{cmd}'), '/C taskkill /IM GorgiHelper.exe /F', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Sleep(800);
  Result := '';
end;

[UninstallRun]
Filename: "{cmd}"; Parameters: "/C taskkill /IM GorgiHelper.exe /F"; Flags: runhidden; RunOnceId: "StopHelper"
