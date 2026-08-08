@echo off
setlocal

set VETO_DIR=%~dp0
set SRC_DIR=%VETO_DIR%src
set INC_DIR=%VETO_DIR%include
set LIB_DIR=%VETO_DIR%lib
set BIN_DIR=%VETO_DIR%bin

if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

echo ==========================================
echo   Building Veto DPI Bypass Engine v1.0.0
echo ==========================================
echo.

set PATH=%PATH%;%LIB_DIR%

gcc -O2 -Wall -I"%INC_DIR%" -I"%LIB_DIR%" -I"%LIB_DIR%\lua" ^
    "%SRC_DIR%\core\packet.c" ^
    "%SRC_DIR%\core\tcp_reassembly.c" ^
    "%SRC_DIR%\core\veto_engine.c" ^
    "%SRC_DIR%\core\veto_lua.c" ^
    "%SRC_DIR%\core\veto_autotune.c" ^
    "%SRC_DIR%\protocols\proto_detect.c" ^
    "%SRC_DIR%\attacks\attacks.c" ^
    "%SRC_DIR%\config\config.c" ^
    "%SRC_DIR%\capture\capture.c" ^
    "%SRC_DIR%\main.c" ^
    -o "%BIN_DIR%\veto.exe" ^
    -L"%LIB_DIR%" ^
    -l:liblua.a -l:libwindivert.a -lws2_32 -ladvapi32 -lshlwapi
if errorlevel 1 goto :error

echo.
echo ==========================================
echo   Build successful!
echo   Binary: %BIN_DIR%\veto.exe
echo ==========================================
goto :end

:error
echo.
echo BUILD FAILED
exit /b 1

:end
endlocal
