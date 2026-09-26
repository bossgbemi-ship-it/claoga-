; Inno Setup script for OJU (Windows x64). Compile with Inno Setup 6: https://jrsoftware.org
; Build the plugin first (build-windows.bat), then open this file in Inno Setup and press Compile.
; Sign the installer and the plugin with your code-signing certificate before release (see docs/SHIPPING.md).

#define AppName "OJU"
#define AppVersion "2.0.0"
#define Company "Made by Joseph"
#define BuildDir "..\..\build-win\OJU_artefacts\Release"

[Setup]
AppId={{6C1E3E5A-5B7B-4E0F-9C1A-0A4F3D2B7E11}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#Company}
AppPublisherURL=https://madebyjoseph.com
DefaultDirName={autopf}\{#Company}\{#AppName}
DefaultGroupName={#Company}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputBaseFilename=OJU-{#AppVersion}-Windows
OutputDir=output
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; SignTool=mysigntool   ; configure in Tools > Configure Sign Tools

[Types]
Name: "full"; Description: "VST3 and Standalone"
Name: "custom"; Description: "Choose components"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (Reaper, FL Studio, Ableton Live, Cubase...)"; Types: full custom; Flags: fixed
Name: "app";  Description: "Standalone app"; Types: full custom

[Files]
Source: "{#BuildDir}\VST3\OJU.vst3\*"; DestDir: "{commoncf64}\VST3\OJU.vst3"; Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#BuildDir}\Standalone\OJU.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "..\..\third_party\rnnoise\COPYING"; DestDir: "{app}\Licences"; DestName: "RNNoise (BSD-3-Clause).txt"; Flags: ignoreversion

[Icons]
Name: "{group}\OJU"; Filename: "{app}\OJU.exe"; Components: app

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\OJU.vst3"
