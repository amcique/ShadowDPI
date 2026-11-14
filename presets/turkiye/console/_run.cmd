@echo off
rem Shared runner for the console launchers in this folder. Use one of the
rem numbered scripts instead of starting this file directly.
setlocal EnableExtensions
call "%~dp0..\settings.cmd"

set "SDP_PRESET=%~1"
set "SDP_LABEL=%~2"
set "SDP_ARGS="
if "%SDP_PRESET%"=="1" set "SDP_ARGS=%SDP_ARGS_1%"
if "%SDP_PRESET%"=="2" set "SDP_ARGS=%SDP_ARGS_2%"
if "%SDP_PRESET%"=="3" set "SDP_ARGS=%SDP_ARGS_3%"
if "%SDP_PRESET%"=="4" set "SDP_ARGS=%SDP_ARGS_4%"
if "%SDP_PRESET%"=="5" set "SDP_ARGS=%SDP_ARGS_5%"
if "%SDP_PRESET%"=="6" set "SDP_ARGS=%SDP_ARGS_6%"

if not defined SDP_ARGS goto bad_preset
if not exist "%SDP_EXE%" goto missing_exe

title ShadowDPI - Turkey %SDP_PRESET% - %SDP_LABEL%
echo ShadowDPI Turkey preset %SDP_PRESET%: %SDP_LABEL%
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
echo Unknown preset "%SDP_PRESET%". Expected a number from 1 to 6.
pause
exit /b 2

:missing_exe
echo shadowdpi.exe was not found at:
echo   %SDP_EXE%
echo.
echo Unpack the release or build the project so that shadowdpi.exe sits next
echo to the presets folder.
pause
exit /b 3
