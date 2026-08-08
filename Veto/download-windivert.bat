@echo off
setlocal

set WINDIVERT_URL=https://github.com/basil00/WinDivert/releases/download/v2.2.2/WinDivert-2.2.2-A.zip
set LIB_DIR=%~dp0lib
set TEMP_DIR=%TEMP%\windivert_download

if not exist "%LIB_DIR%" mkdir "%LIB_DIR%"
if not exist "%TEMP_DIR%" mkdir "%TEMP_DIR%"

echo Downloading WinDivert 2.2.2...
powershell -Command "& { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%WINDIVERT_URL%' -OutFile '%TEMP_DIR%\windivert.zip' }"
if errorlevel 1 (
    echo Failed to download WinDivert
    echo Please download manually from: https://github.com/basil00/WinDivert/releases
    exit /b 1
)

echo Extracting...
powershell -Command "Expand-Archive -Path '%TEMP_DIR%\windivert.zip' -DestinationPath '%TEMP_DIR%\windivert' -Force"

echo Copying files...
copy "%TEMP_DIR%\windivert\include\windivert.h" "%LIB_DIR%\" >nul
copy "%TEMP_DIR%\windivert\lib\x64\windivert.lib" "%LIB_DIR%\" >nul
copy "%TEMP_DIR%\windivert\lib\x64\windivert.dll" "%LIB_DIR%\" >nul
copy "%TEMP_DIR%\windivert\lib\x64\windivert.sys" "%LIB_DIR%\" >nul

echo Cleaning up...
rmdir /s /q "%TEMP_DIR%"

echo.
echo WinDivert installed to: %LIB_DIR%
echo Files:
dir /b "%LIB_DIR%\windivert.*"
