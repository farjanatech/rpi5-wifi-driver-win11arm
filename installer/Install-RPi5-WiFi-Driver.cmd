@echo off
setlocal
title Raspberry Pi 5 Wi-Fi Driver Installer
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-test-driver.ps1"
exit /b %ERRORLEVEL%
