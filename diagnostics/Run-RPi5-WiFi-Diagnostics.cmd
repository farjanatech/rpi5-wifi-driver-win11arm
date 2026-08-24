@echo off
setlocal
title Raspberry Pi 5 Wi-Fi Diagnostics
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Collect-RPi5-WiFi-Diagnostics.ps1"
exit /b %ERRORLEVEL%
