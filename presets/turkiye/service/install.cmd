@echo off
rem Install ShadowDPI as an auto-start Windows service with one Turkey preset.
rem Usage: install.cmd [1-6]. Without an argument the script asks for a number.
setlocal EnableExtensions
call "%~dp0..\settings.cmd"

set "SDP_SERVICE=ShadowDPI"

net session >nul 2>&1
if errorlevel 1 goto need_admin

set "SDP_PRESET=%~1"
if defined SDP_PRESET goto select

:menu
echo.
echo   ShadowDPI Turkey presets
echo   ------------------------
echo   1  -5 --fake-ttl 5 plus DNS redirect
echo   2  -5
echo   3  --fake-ttl 3 plus DNS redirect
echo   4  -5 plus DNS redirect
echo   5  -9 plus DNS redirect
echo   6  -9
echo.
set "SDP_PRESET="
set /p "SDP_PRESET=Preset number [1-6]: "

:select
set "SDP_ARGS="
if "%SDP_PRESET%"=="1" set "SDP_ARGS=%SDP_ARGS_1%"
if "%SDP_PRESET%"=="2" set "SDP_ARGS=%SDP_ARGS_2%"
if "%SDP_PRESET%"=="3" set "SDP_ARGS=%SDP_ARGS_3%"
if "%SDP_PRESET%"=="4" set "SDP_ARGS=%SDP_ARGS_4%"
if "%SDP_PRESET%"=="5" set "SDP_ARGS=%SDP_ARGS_5%"
if "%SDP_PRESET%"=="6" set "SDP_ARGS=%SDP_ARGS_6%"

if not defined SDP_ARGS goto bad_preset
if not exist "%SDP_EXE%" goto missing_exe

echo.
echo Installing the ShadowDPI service with preset %SDP_PRESET%.
echo Command: "%SDP_EXE%" %SDP_ARGS%
echo.

rem Replace any previous installation. The WinDivert driver services are
rem shared with other tools and are not touched here.
sc stop "%SDP_SERVICE%" >nul 2>&1
sc delete "%SDP_SERVICE%" >nul 2>&1

sc create "%SDP_SERVICE%" binPath= "\"%SDP_EXE%\" %SDP_ARGS%" start= auto
if errorlevel 1 goto create_failed

sc description "%SDP_SERVICE%" "ShadowDPI, Turkey preset %SDP_PRESET%"
sc start "%SDP_SERVICE%"
if errorlevel 1 goto start_failed

echo.
echo Done. The service starts automatically at boot.
echo Remove it with service\remove.cmd.
pause
exit /b 0

:start_failed
echo.
echo The service was created but did not start.
echo Run "sc query %SDP_SERVICE%" and check the Windows event log.
pause
exit /b 4

:need_admin
echo This script must run as Administrator.
echo Right-click it and choose "Run as administrator".
pause
exit /b 1

:bad_preset
echo Unknown preset "%SDP_PRESET%". Expected a number from 1 to 6.
pause
exit /b 2

:missing_exe
echo shadowdpi.exe was not found at:
echo   %SDP_EXE%
pause
exit /b 3

:create_failed
echo The service could not be created.
pause
exit /b 5
