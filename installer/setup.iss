; HLPlayer Installer — Inno Setup Script
; Usage: Install Inno Setup (https://jrsoftware.org/isdl.php) then run: ISCC installer\setup.iss

#define MyAppName "HLPlayer"
#define MyAppVersion "1.6"
#define MyAppPublisher "HLPlayer"
#define MyAppURL "https://github.com/hlplayer"
#define MyAppExeName "hlplayer_app.exe"
#define BuildDir "build\src\app"

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
; Use our custom icon
SetupIconFile=resources\icons\video.ico
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
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式 (&Desktop)"; GroupDescription: "附加图标:"; Flags: unchecked
Name: "hlvassoc"; Description: "关联 .hlv 加密视频文件 (&Associate)"; GroupDescription: "文件关联:"; Flags: checkedonce

[Files]
; Application icon (for .hlv file association)
Source: "resources\icons\video.ico"; DestDir: "{app}"; Flags: ignoreversion

; Main executable and all DLLs (recursive)
Source: "{#BuildDir}\hlplayer_app.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs
; Qt platform plugins
Source: "{#BuildDir}\platforms\*.dll"; DestDir: "{app}\platforms"; Flags: ignoreversion
; Qt style plugins
Source: "{#BuildDir}\styles\*.dll"; DestDir: "{app}\styles"; Flags: ignoreversion
; Qt image format plugins
Source: "{#BuildDir}\imageformats\*.dll"; DestDir: "{app}\imageformats"; Flags: ignoreversion
; Qt icon engines
Source: "{#BuildDir}\iconengines\*.dll"; DestDir: "{app}\iconengines"; Flags: ignoreversion
; Qt TLS backends
Source: "{#BuildDir}\tls\*.dll"; DestDir: "{app}\tls"; Flags: ignoreversion
; Qt network information
Source: "{#BuildDir}\networkinformation\*.dll"; DestDir: "{app}\networkinformation"; Flags: ignoreversion
; Qt multimedia
Source: "{#BuildDir}\multimedia\*.dll"; DestDir: "{app}\multimedia"; Flags: ignoreversion
; Qt SQL drivers
Source: "{#BuildDir}\sqldrivers\*.dll"; DestDir: "{app}\sqldrivers"; Flags: ignoreversion
; Qt generic plugins
Source: "{#BuildDir}\generic\*.dll"; DestDir: "{app}\generic"; Flags: ignoreversion
; Qt geometry loaders
Source: "{#BuildDir}\geometryloaders\*.dll"; DestDir: "{app}\geometryloaders"; Flags: ignoreversion
; Qt render plugins
Source: "{#BuildDir}\renderplugins\*.dll"; DestDir: "{app}\renderplugins"; Flags: ignoreversion
; Qt scene parsers
Source: "{#BuildDir}\sceneparsers\*.dll"; DestDir: "{app}\sceneparsers"; Flags: ignoreversion
; Qt renderers
Source: "{#BuildDir}\renderers\*.dll"; DestDir: "{app}\renderers"; Flags: ignoreversion
; Qt translations
Source: "{#BuildDir}\translations\*.qm"; DestDir: "{app}\translations"; Flags: ignoreversion
; QML modules
Source: "{#BuildDir}\qml\*"; DestDir: "{app}\qml"; Flags: ignoreversion recursesubdirs createallsubdirs
; QML tooling
Source: "{#BuildDir}\qmltooling\*.dll"; DestDir: "{app}\qmltooling"; Flags: ignoreversion
; Platform input contexts (virtual keyboard)
Source: "{#BuildDir}\platforminputcontexts\*.dll"; DestDir: "{app}\platforminputcontexts"; Flags: ignoreversion

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
