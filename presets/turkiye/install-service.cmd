@echo off
rem Shared service installer. A provider launcher loads that provider's
rem settings and then calls this file. Pass a recipe number to skip the menu,
rem for example: install.cmd 3
setlocal EnableExtensions

if not defined SDP_COUNT goto missing_settings

net session >nul 2>&1
if errorlevel 1 goto need_admin

if "%~1"=="" goto menu
set "SDP_PRESET=%~1"
goto validate

:menu
echo.
echo   ShadowDPI recipes for %SDP_PROVIDER%
echo   ---------------------------------
for /l %%I in (1,1,%SDP_COUNT%) do call echo    %%I  %%SDP_LABEL_%%I%%
echo.
choice /c %SDP_CHOICES% /n /m "Recipe number: "
set "SDP_PRESET=%ERRORLEVEL%"

:validate
set "SDP_VALID="
for /l %%I in (1,1,%SDP_COUNT%) do if "%SDP_PRESET%"=="%%I" set "SDP_VALID=1"
if not defined SDP_VALID goto bad_preset

call set "SDP_LABEL=%%SDP_LABEL_%SDP_PRESET%%%"
call set "SDP_ARGS=%%SDP_ARGS_%SDP_PRESET%%%"
if not defined SDP_ARGS goto bad_preset
if not exist "%SDP_EXE%" goto missing_exe

echo.
echo Installing the ShadowDPI service with %SDP_PROVIDER% recipe %SDP_PRESET%.
echo Recipe: %SDP_LABEL%
echo Command: "%SDP_EXE%" %SDP_ARGS%
echo.

rem Replace any previous installation. The WinDivert driver services are
rem shared with other tools and are not touched here.
sc stop "ShadowDPI" >nul 2>&1
sc delete "ShadowDPI" >nul 2>&1

sc create "ShadowDPI" binPath= "\"%SDP_EXE%\" %SDP_ARGS%" start= auto
if errorlevel 1 goto create_failed

sc description "ShadowDPI" "ShadowDPI, %SDP_PROVIDER% recipe %SDP_PRESET%"
sc start "ShadowDPI"
if errorlevel 1 goto start_failed

echo.
echo Done. The service starts automatically at boot.
echo Remove it with the remove.cmd script in the same service folder.
pause
exit /b 0

:start_failed
echo.
echo The service was created but did not start.
echo Run "sc query ShadowDPI" and check the Windows event log.
pause
exit /b 4

:need_admin
echo This script must run as Administrator.
echo Right-click it and choose "Run as administrator".
pause
exit /b 1

:bad_preset
echo Unknown recipe "%SDP_PRESET%" for %SDP_PROVIDER%.
pause
exit /b 2

:missing_settings
echo This script must be started through a provider launcher.
pause
exit /b 3

:missing_exe
echo shadowdpi.exe was not found at:
echo   %SDP_EXE%
pause
exit /b 5

:create_failed
echo The service could not be created.
pause
exit /b 6
