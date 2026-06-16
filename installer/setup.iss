; HLPlayer Installer — Inno Setup Script
; Usage: Install Inno Setup (https://jrsoftware.org/isdl.php) then run: ISCC installer\setup.iss

#define MyAppName "HLPlayer"
#define MyAppVersion "1.6"
#define MyAppPublisher "HLPlayer"
#define MyAppURL "https://github.com/hlplayer"
#define MyAppExeName "hlplayer_app.exe"
#define BuildDir "..\build\src\app"

[Setup]
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
; Allow user to change install directory (not forced to C:)
DisableDirPage=no
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
; Output installer to installer\ directory
OutputDir=installer
OutputBaseFilename=HLPlayer_Setup_v{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
; No "Install for all users" prompt — installs for current user by default
PrivilegesRequired=lowest
; Uninstall display name
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式 (&Desktop)"; GroupDescription: "附加图标:"; Flags: unchecked
Name: "hlvassoc"; Description: "关联 .hlv 加密视频文件 (&Associate)"; GroupDescription: "文件关联:"; Flags: checkedonce

[Files]
; Application icon (for .hlv file association)
Source: "..\resources\icons\video.ico"; DestDir: "{app}"; Flags: ignoreversion

; Main executable
Source: "{#BuildDir}\hlplayer_app.exe"; DestDir: "{app}"; Flags: ignoreversion
; All DLLs and subdirectories (Qt plugins, QML modules, etc.)
; Exclude build artifacts
Source: "{#BuildDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\*.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\qml\*"; DestDir: "{app}\qml"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\qmltooling\*"; DestDir: "{app}\qmltooling"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\iconengines\*"; DestDir: "{app}\iconengines"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\imageformats\*"; DestDir: "{app}\imageformats"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\styles\*"; DestDir: "{app}\styles"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\tls\*"; DestDir: "{app}\tls"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\sqldrivers\*"; DestDir: "{app}\sqldrivers"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\generic\*"; DestDir: "{app}\generic"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\geometryloaders\*"; DestDir: "{app}\geometryloaders"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\renderplugins\*"; DestDir: "{app}\renderplugins"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\renderers\*"; DestDir: "{app}\renderers"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\sceneparsers\*"; DestDir: "{app}\sceneparsers"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\platforminputcontexts\*"; DestDir: "{app}\platforminputcontexts"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\translations\*"; DestDir: "{app}\translations"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#BuildDir}\multimedia\*"; DestDir: "{app}\multimedia"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist

[Icons]
; Start Menu shortcut
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
; Desktop shortcut (if user chose it)
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
; Uninstall shortcut
Name: "{group}\卸载 HLPlayer"; Filename: "{uninstallexe}"

[Registry]
; .hlv file association (only if user chose it)
Root: HKCU; Subkey: "Software\Classes\.hlv"; ValueType: string; ValueData: "HLPlayer.hlv"; Flags: uninsdeletekey; Tasks: hlvassoc
Root: HKCU; Subkey: "Software\Classes\HLPlayer.hlv"; ValueType: string; ValueData: "HLPlayer 加密视频"; Flags: uninsdeletekey; Tasks: hlvassoc
; Set icon for .hlv files (uses separate .ico file in app dir)
Root: HKCU; Subkey: "Software\Classes\HLPlayer.hlv\DefaultIcon"; ValueType: string; ValueData: "{app}\video.ico"; Tasks: hlvassoc
; Double-click to open
Root: HKCU; Subkey: "Software\Classes\HLPlayer.hlv\shell\open\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: hlvassoc

[Run]
; Launch after install
Filename: "{app}\{#MyAppExeName}"; Description: "启动 HLPlayer"; Flags: nowait postinstall skipifsilent
