@echo off
setlocal
powershell -ExecutionPolicy Bypass -File "%~dp0package-windows.ps1" %*
endlocal

