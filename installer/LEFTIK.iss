; LEFTIK 2026.1 x64 installer (Inno Setup 6.7+)
;
; Signing (beta, self-signed):
;   1) signtool sign /fd SHA256 /f LEFTIK_Beta_CodeSigning.pfx /p <pass> ^
;        /tr http://timestamp.digicert.com /td SHA256 LEFTIK.exe
;   2) build this script with ISCC.exe
;   3) signtool sign ... LEFTIK_2026.1_x64_Setup.exe
;
; Production must use an EV code-signing certificate instead of the beta cert.
; No auto-updater and no autostart Run entry in 2026.1.

#define AppName "LEFTIK"
#define AppVersion "2026.1"
#define AppPublisher "LEFTIK"
#define AppExeName "LEFTIK.exe"

[Setup]
AppId={{DDC77D79-60DF-463C-979F-8E0602B88C9B}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
VersionInfoVersion=2026.1.0.0
VersionInfoCompany={#AppPublisher}
VersionInfoDescription=LEFTIK Optimizer Setup
VersionInfoCopyright=Copyright (C) 2026 LEFTIK
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
VersionInfoOriginalFileName=LEFTIK_2026.1_x64_Setup.exe

DefaultDirName={autopf}\LEFTIK
DefaultGroupName=LEFTIK
DisableProgramGroupPage=yes
AllowNoIcons=no

; The application writes C:\ProgramData\LEFTIK and its manifest requires
; administrator, so Setup and Uninstall must run elevated as well.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041

OutputDir=..\dist
OutputBaseFilename=LEFTIK_2026.1_x64_Setup
SetupIconFile=..\leftik.ico
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName} {#AppVersion}

Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
russian.CreateDesktopIcon=Создать ярлык на рабочем столе
english.CreateDesktopIcon=Create a &desktop shortcut
russian.AdditionalIcons=Дополнительно:
english.AdditionalIcons=Additional shortcuts:
russian.UninstallShortcut=Удалить LEFTIK
english.UninstallShortcut=Uninstall LEFTIK
russian.UninstallWarningTitle=ВНИМАНИЕ: активные твики
english.UninstallWarningTitle=WARNING: active tweaks
russian.UninstallWarningLine1=Если вы применяли твики и не откатили их, после удаления LEFTIK откат станет невозможен.
english.UninstallWarningLine1=If you applied tweaks and did not roll them back, rollback will be impossible after LEFTIK is removed.
russian.UninstallWarningLine2=Рекомендуется перед удалением запустить LEFTIK → Восстановление → ОТКАТИТЬ ВСЁ.
english.UninstallWarningLine2=Before uninstalling, run LEFTIK → Recovery → ROLL BACK EVERYTHING.
russian.UninstallContinue=Продолжить
english.UninstallContinue=Continue
russian.UninstallCancel=Отмена
english.UninstallCancel=Cancel
russian.UninstallDataLine1=Удалить журнал откатов и настройки LEFTIK?
english.UninstallDataLine1=Delete the rollback journal and LEFTIK settings?
russian.UninstallDataLine2=По умолчанию данные сохраняются: HKCU\Software\LEFTIK и C:\ProgramData\LEFTIK.
english.UninstallDataLine2=By default the data is kept: HKCU\Software\LEFTIK and C:\ProgramData\LEFTIK.
russian.UninstallDataLine3=Нажмите «Нет», чтобы сохранить данные (рекомендуется).
english.UninstallDataLine3=Click No to keep the data (recommended).

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\build-msvc\Release\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Comment: "LEFTIK Optimizer"
Name: "{group}\{cm:UninstallShortcut}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Code]
var
  RemoveUserData: Boolean;

function InitializeUninstall(): Boolean;
begin
  RemoveUserData := False;
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Answer: Integer;
  Labels: TArrayOfString;
begin
  if CurUninstallStep = usUninstall then
  begin
    // Silent uninstall is non-destructive by default: keep journal and settings.
    if UninstallSilent then
    begin
      RemoveUserData := False;
      Exit;
    end;

    SetArrayLength(Labels, 2);
    Labels[0] := ExpandConstant('{cm:UninstallContinue}');
    Labels[1] := ExpandConstant('{cm:UninstallCancel}');

    Answer := TaskDialogMsgBox(
      ExpandConstant('{cm:UninstallWarningTitle}'),
      ExpandConstant('{cm:UninstallWarningLine1}') + #13#10 + #13#10 +
        ExpandConstant('{cm:UninstallWarningLine2}'),
      mbConfirmation, MB_OKCANCEL, Labels, 0);
    if Answer <> IDOK then
      Abort;

    // Default is No (MB_DEFBUTTON2): keep HKCU and ProgramData.
    if MsgBox(
      ExpandConstant('{cm:UninstallDataLine1}') + #13#10 + #13#10 +
        ExpandConstant('{cm:UninstallDataLine2}') + #13#10 +
        ExpandConstant('{cm:UninstallDataLine3}'),
      mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      RemoveUserData := True
    else
      RemoveUserData := False;
  end
  else if CurUninstallStep = usPostUninstall then
  begin
    if RemoveUserData then
    begin
      if RegDeleteKeyIncludingSubkeys(HKCU, 'Software\LEFTIK') then
        Log('LEFTIK: HKCU\Software\LEFTIK removed')
      else
        Log('LEFTIK: HKCU\Software\LEFTIK not removed (missing or access denied)');

      if DelTree(ExpandConstant('{commonappdata}\LEFTIK'), True, True, True) then
        Log('LEFTIK: ProgramData\LEFTIK removed')
      else
        Log('LEFTIK: ProgramData\LEFTIK not removed (missing or access denied)');
    end
    else
      Log('LEFTIK: user data preserved by user choice');
  end;
end;
