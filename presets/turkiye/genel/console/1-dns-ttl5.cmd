@echo off
rem Genel recipe 1: -5 with a fixed fake TTL of 5, plus DNS redirect.
call "%~dp0..\settings.cmd"
call "%~dp0..\..\run.cmd" 1
