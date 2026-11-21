@echo off
setlocal EnableExtensions
title Meridian screen saver - remove
cd /d "%~dp0"

rem Only undo what Install-ScreenSaver.bat did, and only while Windows still points at this copy.
set "CUR="
for /f "tokens=2,*" %%A in ('reg query "HKCU\Control Panel\Desktop" /v SCRNSAVE.EXE 2^>nul') do if /i "%%A"=="REG_SZ" set "CUR=%%B"
if /i "%CUR%"=="%~dp0Meridian.scr" goto :undo
echo Windows is not set to use this Meridian screen saver, so no setting was changed.
goto :done

:undo
rem What was set before the install (if it was saved): put that back
set "BSAVED="
reg query "HKCU\Software\Meridian\ScreenSaverBackup" /v Saved >nul 2>&1 && set "BSAVED=1"
if not defined BSAVED goto :plainoff
set "BSCR="
for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Meridian\ScreenSaverBackup" /v Scr 2^>nul') do if /i "%%A"=="REG_SZ" set "BSCR=%%B"
set "BTYPE=REG_SZ"
for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Meridian\ScreenSaverBackup" /v ScrType 2^>nul') do if /i "%%A"=="REG_SZ" if /i "%%B"=="REG_EXPAND_SZ" set "BTYPE=REG_EXPAND_SZ"
set "BACT="
for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Meridian\ScreenSaverBackup" /v Active 2^>nul') do if /i "%%A"=="REG_SZ" set "BACT=%%B"
set "BWAIT="
for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Meridian\ScreenSaverBackup" /v Wait 2^>nul') do if /i "%%A"=="REG_SZ" set "BWAIT=%%B"
if not defined BSCR goto :noscr
reg add "HKCU\Control Panel\Desktop" /v SCRNSAVE.EXE /t %BTYPE% /d "%BSCR%" /f >nul
goto :scrdone
:noscr
reg delete "HKCU\Control Panel\Desktop" /v SCRNSAVE.EXE /f >nul 2>&1
:scrdone
if not defined BACT set "BACT=0"
reg add "HKCU\Control Panel\Desktop" /v ScreenSaveActive /t REG_SZ /d "%BACT%" /f >nul
if defined BWAIT reg add "HKCU\Control Panel\Desktop" /v ScreenSaveTimeOut /t REG_SZ /d "%BWAIT%" /f >nul
reg delete "HKCU\Software\Meridian\ScreenSaverBackup" /f >nul 2>&1
rundll32.exe user32.dll,UpdatePerUserSystemParameters 1, True
reg delete "HKCU\Software\Meridian\Saver" /v SleepAfterMinutes /f >nul 2>&1
echo Windows no longer starts the Meridian screen saver, and your earlier screen saver settings are back.
goto :cleanup

:plainoff
reg delete "HKCU\Control Panel\Desktop" /v SCRNSAVE.EXE /f >nul 2>&1
reg add "HKCU\Control Panel\Desktop" /v ScreenSaveActive /t REG_SZ /d 0 /f >nul
rundll32.exe user32.dll,UpdatePerUserSystemParameters 1, True
reg delete "HKCU\Software\Meridian\Saver" /v SleepAfterMinutes /f >nul 2>&1
echo Windows no longer starts the Meridian screen saver. To use another one: Settings, Personalization, Lock screen, Screen saver settings.

:cleanup
del /q "Meridian.scr" >nul 2>&1
:done
echo.
pause
