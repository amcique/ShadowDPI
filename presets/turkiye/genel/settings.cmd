@echo off
rem Recipes that are not tied to a single provider. Loaded by the launchers.
call "%~dp0..\settings.cmd"

set "SDP_PROVIDER=Genel"
set "SDP_COUNT=1"
set "SDP_CHOICES=1"

set "SDP_LABEL_1=DNS + fake TTL 5"
set "SDP_ARGS_1=-5 --fake-ttl 5 %SDP_DNS_ARGS%"
