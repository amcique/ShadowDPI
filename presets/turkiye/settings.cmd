@echo off
rem ===========================================================================
rem  ShadowDPI Turkey presets - shared settings
rem
rem  Edit this file only. Every launcher in console\ and service\ reads it.
rem  Run the launchers with Administrator rights.
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

rem The six recipes. A preset comes first and overrides follow it, because
rem later command line arguments win. --fake-ttl turns off the automatic TTL
rem that -5 would otherwise choose.
set "SDP_ARGS_1=-5 --fake-ttl 5 %SDP_DNS_ARGS%"
set "SDP_ARGS_2=-5"
set "SDP_ARGS_3=--fake-ttl 3 %SDP_DNS_ARGS%"
set "SDP_ARGS_4=-5 %SDP_DNS_ARGS%"
set "SDP_ARGS_5=-9 %SDP_DNS_ARGS%"
set "SDP_ARGS_6=-9"
