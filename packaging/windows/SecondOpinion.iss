; Inno Setup 6.3+ script for Second Opinion (Windows x64).
;
; Normally run through build_installer.ps1, which passes:
;   /DAppVersion=0.1.0  /DArtefactsDir=<...\SecondOpinion_artefacts\Release>  /DOutputDir=<...>
; and signs the binaries and the installer when signing variables are set.
;
; Installs:
;   VST3        -> {commoncf64}\VST3\Second Opinion.vst3   (C:\Program Files\Common Files\VST3)
;   Standalone  -> {commonpf64}\Second Opinion             (exe, onnxruntime.dll, Resources\)

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef ArtefactsDir
  #define ArtefactsDir "..\..\build\win-x64-release\SecondOpinion_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\build\packages"
#endif

#define AppName "Second Opinion"
#define Publisher "Berment Audio"

[Setup]
; Fixed AppId: never change it, or upgrades install side by side.
AppId={{6F1D9A52-3C0B-4E5B-9B7E-5D2C8A41F0B3}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Publisher}
DefaultDirName={commonpf64}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
PrivilegesRequired=admin
OutputDir={#OutputDir}
OutputBaseFilename=Second Opinion {#AppVersion} Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\{#AppName}.exe
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#Publisher}
VersionInfoProductName={#AppName}

[Types]
Name: "full"; Description: "VST3 plug-in and Standalone app"
Name: "custom"; Description: "Choose components"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (Common Files\VST3)"; Types: full custom
Name: "standalone"; Description: "Standalone app"; Types: full custom

[Files]
; VST3 bundle: Contents\x86_64-win\Second Opinion.vst3, onnxruntime.dll beside it, Contents\Resources.
Source: "{#ArtefactsDir}\VST3\{#AppName}.vst3\*"; DestDir: "{commoncf64}\VST3\{#AppName}.vst3"; \
    Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
; Standalone folder: exe, onnxruntime.dll, Resources\basic_pitch and licences.
Source: "{#ArtefactsDir}\Standalone\*"; DestDir: "{app}"; Excludes: "*.pdb,*.ilk,*.exp,*.lib"; \
    Components: standalone; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppName}.exe"; Components: standalone

[InstallDelete]
; Remove files an older version may have left inside the bundle (the folder is ours alone).
Type: filesandordirs; Name: "{commoncf64}\VST3\{#AppName}.vst3"; Components: vst3

[UninstallDelete]
Type: dirifempty; Name: "{app}"

[Run]
Filename: "{app}\{#AppName}.exe"; Description: "Open {#AppName}"; Components: standalone; \
    Flags: nowait postinstall skipifsilent unchecked
