@echo off
rem mod-hw-emulator - MOD Desktop with the emulated Dwarf or Duo X, see mhe-desktop.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0mhe-desktop.ps1" %*
