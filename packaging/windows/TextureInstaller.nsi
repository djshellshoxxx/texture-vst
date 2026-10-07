; TEXTURE Windows installer (NSIS 3, Unicode).
; Built by packaging/package_release.sh, which passes:
;   /DVERSION=0.0.1-beta /DSTAGE=<dir with Texture.exe, Texture.vst3, Texture.clap, docs>
;   /DICON=<icon.ico> /DOUTFILE=<setup.exe>
Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel admin
ManifestDPIAware true

!include "MUI2.nsh"
!include "x64.nsh"

!define PRODUCT "TEXTURE"
!define PUBLISHER "Circuit Drift Labs"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Texture"

Name "${PRODUCT} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\Texture"
InstallDirRegKey HKLM "Software\Texture" "InstallDir"
BrandingText "${PRODUCT} ${VERSION}"

!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_RUN "$INSTDIR\Texture.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch the TEXTURE standalone app"
!define MUI_FINISHPAGE_RUN_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGE}\LICENSE.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "TEXTURE requires 64-bit Windows 10 or later."
        Abort
    ${EndIf}
    SetRegView 64
FunctionEnd

Function un.onInit
    SetRegView 64
FunctionEnd

Section "Standalone app" SecStandalone
    SetOutPath "$INSTDIR"
    File "${STAGE}\Texture.exe"
    File "${STAGE}\README.txt"
    File "${STAGE}\LICENSE.txt"
    CreateDirectory "$SMPROGRAMS\TEXTURE"
    CreateShortcut "$SMPROGRAMS\TEXTURE\TEXTURE.lnk" "$INSTDIR\Texture.exe"
    CreateShortcut "$SMPROGRAMS\TEXTURE\Uninstall TEXTURE.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "VST3 plug-in" SecVST3
    SetOutPath "$COMMONFILES64\VST3"
    File /r "${STAGE}\Texture.vst3"
SectionEnd

Section "CLAP plug-in" SecCLAP
    SetOutPath "$COMMONFILES64\CLAP"
    File "${STAGE}\Texture.clap"
SectionEnd

Section "-Uninstaller"
    SetOutPath "$INSTDIR"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "Software\Texture" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "${PRODUCT} ${VERSION}"
    WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${UNINSTKEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1
SectionEnd

LangString DESC_Standalone ${LANG_ENGLISH} "The TEXTURE app, for playing without a DAW."
LangString DESC_VST3 ${LANG_ENGLISH} "VST3 plug-in, installed to Common Files\VST3."
LangString DESC_CLAP ${LANG_ENGLISH} "CLAP plug-in, installed to Common Files\CLAP."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStandalone} $(DESC_Standalone)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecVST3} $(DESC_VST3)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecCLAP} $(DESC_CLAP)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; Presets, samples and settings live in the user's profile and are left in place,
; so reinstalling or upgrading never loses a user's work.
Section "Uninstall"
    Delete "$INSTDIR\Texture.exe"
    Delete "$INSTDIR\README.txt"
    Delete "$INSTDIR\LICENSE.txt"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
    RMDir /r "$COMMONFILES64\VST3\Texture.vst3"
    Delete "$COMMONFILES64\CLAP\Texture.clap"
    Delete "$SMPROGRAMS\TEXTURE\TEXTURE.lnk"
    Delete "$SMPROGRAMS\TEXTURE\Uninstall TEXTURE.lnk"
    RMDir "$SMPROGRAMS\TEXTURE"
    DeleteRegKey HKLM "${UNINSTKEY}"
    DeleteRegKey HKLM "Software\Texture"
SectionEnd
