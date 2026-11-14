@echo off
rem Remove the ShadowDPI service. The WinDivert driver services are shared
rem with other tools and are intentionally left in place.
setlocal EnableExtensions
call "%~dp0..\settings.cmd"

set "SDP_SERVICE=ShadowDPI"

net session >nul 2>&1
if errorlevel 1 goto need_admin

sc stop "%SDP_SERVICE%" >nul 2>&1
sc delete "%SDP_SERVICE%"
if errorlevel 1 goto not_installed

echo.
echo The ShadowDPI service was removed.
pause
exit /b 0

:not_installed
echo.
echo The ShadowDPI service is not installed, or it could not be removed.
pause
exit /b 1

:need_admin
echo This script must run as Administrator.
echo Right-click it and choose "Run as administrator".
pause
exit /b 2
