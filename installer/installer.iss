; DopesCrosshairTool Inno Setup Script
; Allows user to choose install dir, creates Start Menu + Desktop shortcuts, uninstaller

#define MyAppName "Dopes Crosshair Tool"
#define MyAppVersion "1.0.2"
#define MyAppPublisher "DopesAIDevelopment"
#define MyAppURL "https://github.com/Dopemodz420/DopesCrosshairTool.git"
#define MyAppExeName "DopesCrosshairTool.exe"

[Setup]
AppId={{3B4E9A2D-5F1C-4A8B-9E2D-7C1A5B8D9E0F}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
LicenseFile=LicenseAgreement.txt
OutputDir=.
OutputBaseFilename=DopesCrosshairTool-Setup-1.0.2
SetupIconFile=..\resources\app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=yes
RestartApplications=no
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\build\bin\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\bin\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\crosshair_config.json"; DestDir: "{app}"; Flags: ignoreversion; Permissions: users-modify
Source: "..\crosshairs\*"; DestDir: "{app}\crosshairs"; Flags: ignoreversion recursesubdirs createallsubdirs
; custom-crosshairs is user data, just create dir, don't overwrite
; resources not needed at runtime (icon embedded), but include for completeness
Source: "..\resources\app.ico"; DestDir: "{app}\resources"; Flags: ignoreversion

[Dirs]
Name: "{app}\custom-crosshairs"; Permissions: users-modify
Name: "{app}\crosshairs"; Permissions: users-modify

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\resources\app.ico"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; IconFilename: "{app}\resources\app.ico"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\crosshairs"
; Keep user custom crosshairs and configs: do not delete custom-crosshairs or AppData config
