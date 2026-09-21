@echo off
setlocal
title RPi5 Wi-Fi - Connect and performance report
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Test-RPi5-WiFi-Performance.ps1"
exit /b %ERRORLEVEL%
