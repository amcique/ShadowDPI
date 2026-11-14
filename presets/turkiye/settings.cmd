@echo off
rem ===========================================================================
rem  ShadowDPI Turkey presets - shared settings
rem
rem  This file holds the values that every provider group shares: the path to
rem  shadowdpi.exe and the DNS resolvers. Provider recipes live in the genel\
rem  and superonline\ folders. Run the launchers with Administrator rights.
rem ===========================================================================

rem Absolute path to shadowdpi.exe. The default assumes the release layout,
rem where this file sits in presets\turkiye and the executable is two levels
rem up. Define SDP_EXE before calling a launcher to use another location.
set "SDP_ROOT=%~dp0..\.."
for %%I in ("%SDP_ROOT%") do set "SDP_ROOT=%%~fI"
if not defined SDP_EXE set "SDP_EXE=%SDP_ROOT%\shadowdpi.exe"

rem Resolvers used by the recipes that redirect DNS at the packet layer.
rem Leave SDP_DNS6 empty to skip the IPv6 redirect. SDP_DNS4 is required by
rem the DNS recipes, and a port must be greater than zero.
set "SDP_DNS4=77.88.8.8"
set "SDP_DNS4_PORT=1253"
set "SDP_DNS6=2a02:6b8::feed:0ff"
set "SDP_DNS6_PORT=1253"

set "SDP_DNS6_ARGS=--dnsv6-addr %SDP_DNS6% --dnsv6-port %SDP_DNS6_PORT%"
if not defined SDP_DNS6 set "SDP_DNS6_ARGS="
set "SDP_DNS_ARGS=--dns-addr %SDP_DNS4% --dns-port %SDP_DNS4_PORT% %SDP_DNS6_ARGS%"
