@echo off
rem Shared console runner. A provider launcher loads that provider's settings
rem and then calls this file with a recipe number. Do not start it directly.
setlocal EnableExtensions

if not defined SDP_COUNT goto missing_settings

set "SDP_PRESET=%~1"
set "SDP_VALID="
for /l %%I in (1,1,%SDP_COUNT%) do if "%SDP_PRESET%"=="%%I" set "SDP_VALID=1"
if not defined SDP_VALID goto bad_preset

call set "SDP_LABEL=%%SDP_LABEL_%SDP_PRESET%%%"
call set "SDP_ARGS=%%SDP_ARGS_%SDP_PRESET%%%"
if not defined SDP_ARGS goto bad_preset
if not exist "%SDP_EXE%" goto missing_exe

title ShadowDPI - %SDP_PROVIDER% - recipe %SDP_PRESET%
echo Provider: %SDP_PROVIDER%
echo Recipe %SDP_PRESET%: %SDP_LABEL%
echo Command: "%SDP_EXE%" %SDP_ARGS%
echo.
echo Leave this window open while ShadowDPI runs. Press Ctrl+C to stop.
echo.

"%SDP_EXE%" %SDP_ARGS%
set "SDP_EXIT=%ERRORLEVEL%"
echo.
echo ShadowDPI exited with code %SDP_EXIT%.
pause
exit /b %SDP_EXIT%

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
echo.
echo Unpack the release or build the project so that shadowdpi.exe sits next
echo to the presets folder.
pause
exit /b 4
