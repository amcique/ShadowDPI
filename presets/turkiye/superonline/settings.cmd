@echo off
rem Recipes reported to work with Superonline. Loaded by the launchers.
call "%~dp0..\settings.cmd"

set "SDP_PROVIDER=Superonline"
set "SDP_COUNT=5"
set "SDP_CHOICES=12345"

set "SDP_LABEL_1=fragment only"
set "SDP_ARGS_1=-5"
set "SDP_LABEL_2=DNS + fake TTL 3"
set "SDP_ARGS_2=--fake-ttl 3 %SDP_DNS_ARGS%"
set "SDP_LABEL_3=DNS + fragment"
set "SDP_ARGS_3=-5 %SDP_DNS_ARGS%"
set "SDP_LABEL_4=DNS + all tricks"
set "SDP_ARGS_4=-9 %SDP_DNS_ARGS%"
set "SDP_LABEL_5=all tricks"
set "SDP_ARGS_5=-9"
