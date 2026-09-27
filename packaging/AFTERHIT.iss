; AFTERHIT - Windows installer, for Inno Setup 6. Run by CI (.github/workflows/windows.yml)
; after the build and both test suites pass; it only packs what the build produced.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SrcRoot
  #define SrcRoot "..\build\AfterHit_artefacts\Release"
#endif

#define AppName      "AFTERHIT"
#define AppPublisher "Naaman"
#define AppAuthor    "Gussa Naaman"

#if VER < EncodeVer(6,0,0)
  #error AFTERHIT's installer needs Inno Setup 6.
#endif

; Checked when the installer is MADE, not on the user's machine: an empty or
; half-built bundle is a compile error here.
#if !FileExists(SrcRoot + "\VST3\AFTERHIT.vst3\Contents\x86_64-win\AFTERHIT.vst3")
  #error The VST3 bundle is missing or empty - the build did not finish.
#endif
#if !FileExists(SrcRoot + "\Standalone\AFTERHIT.exe")
  #error The standalone was not built.
#endif

[Setup]
AppId={{974C7A5C-18F6-4718-B4F3-94CE1A6A3097}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppCopyright={#AppAuthor}
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription=AFTERHIT - hit, then space
DefaultDirName={autopf}\{#AppPublisher}\{#AppName}
DefaultGroupName={#AppPublisher}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=AFTERHIT-{#AppVersion}-windows
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
InfoBeforeFile=INFO-BEFORE.txt
#ifdef AhIcon
SetupIconFile={#AhIcon}
#endif
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\AFTERHIT.exe
PrivilegesRequired=admin
CloseApplications=yes
RestartApplications=no

[Types]
Name: "full";   Description: "Everything"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (Cubase, Live, Reaper, Bitwig)"; Types: full custom; Flags: checkablealone
Name: "app";  Description: "Standalone application";                      Types: full custom

[Files]
; The VST3 is a folder (bundle): copy it whole.
Source: "{#SrcRoot}\VST3\AFTERHIT.vst3\*"; DestDir: "{commoncf64}\VST3\AFTERHIT.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SrcRoot}\Standalone\AFTERHIT.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "..\README.md";           DestDir: "{app}"; DestName: "AFTERHIT-MANUAL.md"; Flags: ignoreversion
Source: "..\docs\PARAMETERS.md";  DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\AFTERHIT.exe"; Components: app

[UninstallDelete]
Type: dirifempty; Name: "{commoncf64}\VST3\AFTERHIT.vst3"

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  Target: String;
begin
  if (CurStep = ssPostInstall) and WizardIsComponentSelected('vst3') then
  begin
    Target := ExpandConstant('{commoncf64}\VST3\AFTERHIT.vst3\Contents\x86_64-win\AFTERHIT.vst3');
    if not FileExists(Target) then
      MsgBox('INSTALL FAILED: the plug-in is not at' + #13#10 + Target + #13#10#13#10 +
             'Close your DAW and run this installer again as administrator.', mbCriticalError, MB_OK);
  end;
end;
