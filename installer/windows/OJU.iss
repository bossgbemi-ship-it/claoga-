; Inno Setup script for OJU (Windows x64). Compile with Inno Setup 6: https://jrsoftware.org
; CI builds it automatically (see .github/workflows/build.yml). By hand: build the plugin first
; (build-windows.bat), then open this file in Inno Setup and press Compile, or run
;   iscc /DAppVersion=2.0.0 /DBuildDir=C:\path\to\OJU_artefacts\Release installer\windows\OJU.iss
; Sign the installer and the plugin with your code-signing certificate before release (see docs/SHIPPING.md).

#ifndef AppVersion
  #define AppVersion "2.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build-win\OJU_artefacts\Release"
#endif
#define AppName "OJU"
#define Company "Made by Joseph"

[Setup]
; Same AppId as every earlier OJU, so a new version installs straight over the old one.
AppId={{6C1E3E5A-5B7B-4E0F-9C1A-0A4F3D2B7E11}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Company}
AppPublisherURL=https://madebyjoseph.com
DefaultDirName={autopf}\{#Company}\{#AppName}
DefaultGroupName={#Company}
DisableProgramGroupPage=yes
DisableDirPage=yes
DisableReadyPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputBaseFilename=OJU-{#AppVersion}-Windows-Setup
OutputDir=output
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\OJU.exe
; SignTool=mysigntool   ; configure in Tools > Configure Sign Tools

[Types]
Name: "full"; Description: "VST3 plugin and Standalone app"
Name: "custom"; Description: "Choose components"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (Ableton Live, FL Studio, Reaper, Cubase, Studio One...)"; Types: full custom; Flags: fixed
Name: "app";  Description: "Standalone app (record without a DAW)"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcut for the Standalone app"; Components: app; Flags: unchecked

[Files]
Source: "{#BuildDir}\VST3\OJU.vst3\*"; DestDir: "{commoncf64}\VST3\OJU.vst3"; Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#BuildDir}\Standalone\OJU.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "..\..\third_party\rnnoise\COPYING"; DestDir: "{app}\Licences"; DestName: "RNNoise (BSD-3-Clause).txt"; Flags: ignoreversion

[Icons]
Name: "{group}\OJU"; Filename: "{app}\OJU.exe"; Components: app
Name: "{autodesktop}\OJU"; Filename: "{app}\OJU.exe"; Components: app; Tasks: desktopicon

[Run]
Filename: "{app}\OJU.exe"; Description: "Open the OJU Standalone app now"; Components: app; Flags: nowait postinstall skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\OJU.vst3"

[Messages]
WelcomeLabel2=This installs OJU {#AppVersion}, the vocal chain that listens.%n%nClose your DAW before you continue.
FinishedHeadingLabel=OJU is installed
FinishedLabel=The plugin is in C:\Program Files\Common Files\VST3.%n%nOpen your DAW and look for OJU under VST3 effects (Made by Joseph). If it doesn't show up, rescan:%n- Ableton Live: Settings > Plug-ins > Rescan%n- FL Studio: Options > Manage plugins > Find more plugins%n- Reaper: Options > Preferences > Plug-ins > VST > Re-scan
