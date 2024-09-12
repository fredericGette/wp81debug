DEL *.obj
DEL *.sys

SETLOCAL
SET PATH=C:\Program Files (x86)\Microsoft Visual Studio 12.0\Common7\IDE\;C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\bin\x86_arm;C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\bin;%PATH%

CL.exe /c /I"C:\Program Files (x86)\Windows Phone Kits\8.1\Include\km" /I"C:\Program Files (x86)\Windows Kits\8.1\Include\Shared" /I"C:\Program Files (x86)\Windows Kits\8.1\Include\km" /I"C:\Program Files (x86)\Windows Kits\8.1\Include\wdf\kmdf\1.11" /I"C:\Program Files (x86)\Windows Kits\8.1\Include\km\crt" /Zi /W4 /Od /D _ARM_ /D ARM /D _USE_DECLSPECS_FOR_SAL=1 /D STD_CALL /D DEPRECATE_DDK_FUNCTIONS=1 /D MSC_NOOPT /D _WIN32_WINNT=0x0602 /D WINVER=0x0602 /D WINNT=1 /D NTDDI_VERSION=0x06020000 /D DBG=1 /D _ARM_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE=1 /D KMDF_VERSION_MAJOR=1 /D KMDF_VERSION_MINOR=11 /Zp8 /Gy /Zc:wchar_t- /Zc:forScope- /GR- /FI"C:\Program Files (x86)\Windows Kits\8.1\Include\Shared\warning.h" /kernel /GF -cbstring /d1import_no_registry /d2AllowCompatibleILVersions /d2Zi+ legacy_driver.c

IF %ERRORLEVEL% NEQ 0 (
	GOTO :EOF
)

link.exe  /VERSION:"6.3" /INCREMENTAL:NO /LIBPATH:"C:\Program Files (x86)\Windows Phone Kits\8.1\lib\win8\km\ARM" /WX "C:\Program Files (x86)\Windows Kits\8.1\lib\winv6.3\UM\ARM\armrt.lib" "C:\Program Files (x86)\Windows Kits\8.1\lib\win8\KM\arm\BufferOverflowFastFailK.lib" "C:\Program Files (x86)\Windows Kits\8.1\lib\win8\KM\arm\ntoskrnl.lib" "C:\Program Files (x86)\Windows Kits\8.1\lib\win8\KM\arm\hal.lib" "C:\Program Files (x86)\Windows Kits\8.1\lib\win8\KM\arm\wmilib.lib" "C:\Program Files (x86)\Windows Kits\8.1\lib\wdf\kmdf\arm\1.11\WdfLdr.lib" "C:\Program Files (x86)\Windows Kits\8.1\lib\wdf\kmdf\arm\1.11\WdfDriverEntry.lib" "C:\Program Files (x86)\Windows Kits\8.1\Lib\winv6.3\km\arm\wdmsec.lib" /NODEFAULTLIB /NODEFAULTLIB:oldnames.lib /MANIFEST:NO /DEBUG /SUBSYSTEM:NATIVE,"6.02" /STACK:"0x40000","0x2000" /Driver /OPT:REF /OPT:ICF /ENTRY:"FxDriverEntry" /RELEASE  /MERGE:"_TEXT=.text;_PAGE=PAGE" /MACHINE:ARM /PROFILE /kernel /IGNORE:4078,4221,4198 /osversion:6.3 /pdbcompress /debugtype:pdata legacy_driver.obj

IF %ERRORLEVEL% NEQ 0 (
	GOTO :EOF
)

REN legacy_driver.sys wp81dbgPrint.sys

"C:\Program Files (x86)\Windows Kits\8.1\bin\x86\signtool.exe" sign /ph /fd "sha256" /f MySPC.pfx wp81dbgPrint.sys

IF %ERRORLEVEL% NEQ 0 (
	GOTO :EOF
)

ECHO Build successful