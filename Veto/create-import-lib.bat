@echo off
setlocal

set LIB_DIR=%~dp0lib

echo Creating MinGW import library from WinDivert.dll...

REM Try to use gendef + dlltool to create .a file
where gendef >nul 2>&1
if errorlevel 1 (
    echo gendef not found, trying alternative method...
    
    REM Create .def file manually
    echo LIBRARY WinDivert > "%LIB_DIR%\windivert.def"
    echo EXPORTS >> "%LIB_DIR%\windivert.def"
    echo     WinDivertOpen >> "%LIB_DIR%\windivert.def"
    echo     WinDivertClose >> "%LIB_DIR%\windivert.def"
    echo     WinDivertRecv >> "%LIB_DIR%\windivert.def"
    echo     WinDivertSend >> "%LIB_DIR%\windivert.def"
    echo     WinDivertShutdown >> "%LIB_DIR%\windivert.def"
    echo     WinDivertSetParam >> "%LIB_DIR%\windivert.def"
    echo     WinDivertGetParam >> "%LIB_DIR%\windivert.def"
) else (
    gendef "%LIB_DIR%\WinDivert.dll" -o "%LIB_DIR%\windivert.def"
)

REM Create import library
dlltool -d "%LIB_DIR%\windivert.def" -l "%LIB_DIR%\libwindivert.a" -D WinDivert.dll

if exist "%LIB_DIR%\libwindivert.a" (
    echo Success! Created libwindivert.a
) else (
    echo Failed to create import library
    exit /b 1
)
