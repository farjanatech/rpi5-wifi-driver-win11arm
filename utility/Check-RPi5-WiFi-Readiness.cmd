@echo off
setlocal
title RPi5 Wi-Fi - Readiness and performance report
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check-RPi5-WiFi-Readiness.ps1" %*
exit /b %ERRORLEVEL%
