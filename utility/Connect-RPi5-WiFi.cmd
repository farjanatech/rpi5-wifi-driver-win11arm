@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell.exe -Verb RunAs -ArgumentList '-NoProfile -NoExit -ExecutionPolicy Bypass -File ""%~dp0Connect-RPi5-WiFi.ps1""'"
