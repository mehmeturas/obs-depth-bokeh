; Inno Setup script for obs-depth-bokeh
;
; Produces the familiar Next / Next / Install wizard. It finds the OBS folder
; from the registry rather than guessing, so portable and non-default installs
; are handled too.

#define AppName "OBS Depth Bokeh"
#define AppVersion "1.0.0"
#define AppPublisher "obs-depth-bokeh"
#define PluginId "obs-depth-bokeh"

#ifndef StageDir
  #define StageDir "..\stage"
#endif
#ifndef OutDir
  #define OutDir "..\dist"
#endif

[Setup]
AppId={{9C4E1A72-3B5D-4F88-9E21-7A6D0C3F1B42}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={code:GetOBSDir}
DisableDirPage=no
DisableProgramGroupPage=yes
OutputDir={#OutDir}
OutputBaseFilename=obs-depth-bokeh-{#AppVersion}-windows-x64-installer
Compression=lzma2/max
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName={#AppName}

[Languages]
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
turkish.DirPageCaption=OBS Studio klasörü
turkish.DirPageDescription=Eklenti nereye kurulsun?
turkish.DirPageLabel=Kurulum, OBS Studio klasörünü otomatik buldu. Farklı bir yere kurduysan aşağıdan değiştir.
turkish.ModelTask=Derinlik modelini kur (~100 MB, gerekli)
turkish.OBSRunning=OBS Studio çalışıyor. Lütfen kapatıp tekrar dene.
english.DirPageCaption=OBS Studio folder
english.DirPageDescription=Where should the plugin be installed?
english.DirPageLabel=Setup detected your OBS Studio folder. Change it below if you installed OBS elsewhere.
english.ModelTask=Install depth model (~100 MB, required)
english.OBSRunning=OBS Studio is running. Please close it and try again.

[Tasks]
Name: "model"; Description: "{cm:ModelTask}"; GroupDescription: "Components"; Flags: checkedonce

[Files]
; Plugin binary and its runtime dependencies
Source: "{#StageDir}\bin\*.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion

; Shader and locale data
Source: "{#StageDir}\data\*"; DestDir: "{app}\data\obs-plugins\{#PluginId}"; Flags: ignoreversion recursesubdirs createallsubdirs

; Depth model
Source: "{#StageDir}\models\*"; DestDir: "{app}\data\obs-plugins\{#PluginId}\models"; Flags: ignoreversion; Tasks: model

; Documentation
Source: "{#StageDir}\KURULUM.md"; DestDir: "{app}\data\obs-plugins\{#PluginId}"; Flags: ignoreversion

[Code]

function GetOBSDir(Param: String): String;
var
  Path: String;
begin
  { OBS records its install path here on a normal install. }
  if RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Path) then
  begin
    Result := Path;
    Exit;
  end;
  if RegQueryStringValue(HKLM32, 'SOFTWARE\OBS Studio', '', Path) then
  begin
    Result := Path;
    Exit;
  end;
  Result := ExpandConstant('{autopf}\obs-studio');
end;

function IsOBSRunning(): Boolean;
var
  ResultCode: Integer;
begin
  { tasklist returns 0 and prints the process when it is running. Using
    findstr keeps this dependency-free. }
  Result := Exec(ExpandConstant('{cmd}'),
    '/C tasklist /FI "IMAGENAME eq obs64.exe" | findstr /I obs64.exe',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  if IsOBSRunning() then
  begin
    MsgBox(ExpandConstant('{cm:OBSRunning}'), mbError, MB_OK);
    Result := False;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpSelectDir then
  begin
    WizardForm.SelectDirLabel.Caption := ExpandConstant('{cm:DirPageLabel}');
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    { A valid OBS folder always has obs64.exe under bin\64bit. Catching this
      here beats installing into a random folder and leaving the user to
      wonder why the filter never appears. }
    if not FileExists(WizardDirValue + '\bin\64bit\obs64.exe') then
    begin
      if MsgBox('obs64.exe bu klasörde bulunamadı. Yine de devam edilsin mi?' + #13#10 +
                'obs64.exe was not found in this folder. Continue anyway?',
                mbConfirmation, MB_YESNO) = IDNO then
        Result := False;
    end;
  end;
end;

[UninstallDelete]
Type: filesandordirs; Name: "{app}\data\obs-plugins\{#PluginId}"
