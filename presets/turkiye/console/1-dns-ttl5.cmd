@echo off
rem Turkey preset 1: -5 with a fixed fake TTL of 5, plus DNS redirect.
call "%~dp0_run.cmd" 1 "DNS + fake TTL 5"
