@echo off
rem Superonline recipe 2: a fixed fake TTL of 3, plus DNS redirect.
call "%~dp0..\settings.cmd"
call "%~dp0..\..\run.cmd" 2
