@echo off
setlocal

set VETO_DIR=%~dp0
set SRC_DIR=%VETO_DIR%src
set INC_DIR=%VETO_DIR%include
set LIB_DIR=%VETO_DIR%lib
set BIN_DIR=%VETO_DIR%bin

if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"
if not exist "%LIB_DIR%" mkdir "%LIB_DIR%"

echo ==========================================
echo   Building Veto DPI Bypass Engine v1.0.0
echo ==========================================
echo.

echo [1/4] Compiling core modules...
cl /nologo /O2 /W3 /I"%INC_DIR%" /I"%LIB_DIR%" ^
   /c "%SRC_DIR%\core\packet.c" /Fo:"%LIB_DIR%\packet.obj"
if errorlevel 1 goto :error

cl /nologo /O2 /W3 /I"%INC_DIR%" /I"%LIB_DIR%" ^
   /c "%SRC_DIR%\core\tcp_reassembly.c" /Fo:"%LIB_DIR%\tcp_reassembly.obj"
if errorlevel 1 goto :error

cl /nologo /O2 /W3 /I"%INC_DIR%" /I"%LIB_DIR%" ^
   /c "%SRC_DIR%\core\veto_engine.c" /Fo:"%LIB_DIR%\veto_engine.obj"
if errorlevel 1 goto :error

echo [2/4] Compiling protocols...
cl /nologo /O2 /W3 /I"%INC_DIR%" /I"%LIB_DIR%" ^
   /c "%SRC_DIR%\protocols\proto_detect.c" /Fo:"%LIB_DIR%\proto_detect.obj"
if errorlevel 1 goto :error

echo [3/4] Compiling attacks...
cl /nologo /O2 /W3 /I"%INC_DIR%" /I"%LIB_DIR%" ^
   /c "%SRC_DIR%\attacks\attacks.c" /Fo:"%LIB_DIR%\attacks.obj"
if errorlevel 1 goto :error

echo [4/4] Linking Veto...
cl /nologo /O2 /Fe:"%BIN_DIR%\veto.exe" ^
   "%LIB_DIR%\packet.obj" ^
   "%LIB_DIR%\tcp_reassembly.obj" ^
   "%LIB_DIR%\veto_engine.obj" ^
   "%LIB_DIR%\proto_detect.obj" ^
   "%LIB_DIR%\attacks.obj" ^
   "%SRC_DIR%\main.c" ^
   /I"%INC_DIR%" /I"%LIB_DIR%" ^
   windivert.lib ws2_32.lib advapi32.lib
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
