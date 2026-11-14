@echo off
rem Install a Superonline recipe as the ShadowDPI service.
call "%~dp0..\settings.cmd"
call "%~dp0..\..\install-service.cmd" %*
