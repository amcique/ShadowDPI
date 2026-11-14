@echo off
rem Shared service remover. The WinDivert driver services are shared with
rem other tools and are intentionally left in place.
setlocal EnableExtensions

net session >nul 2>&1
if errorlevel 1 goto need_admin

sc stop "ShadowDPI" >nul 2>&1
sc delete "ShadowDPI"
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
