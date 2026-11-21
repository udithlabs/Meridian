@echo off
setlocal EnableExtensions
title Meridian screen saver - install
cd /d "%~dp0"

if not exist "Meridian.exe" (
  echo Meridian.exe was not found next to this file.
  echo Unzip the whole Meridian folder first, then run this again from inside it.
  echo.
  pause
  exit /b 1
)

rem How long Windows waits before it starts the screen saver: 5 minutes, or the number (1 to 60) you type after the name,
rem for example:  Install-ScreenSaver.bat 10
set "MINUTES=%~1"
if "%MINUTES%"=="" set "MINUTES=5"
rem Only plain digits count. (set /a reads 08 and 09 as bad octal numbers and 010 as 8, so any zeros in front are cut off first.)
set "BAD="
for /f "delims=0123456789" %%X in ("%MINUTES%") do set "BAD=1"
if defined BAD (
  echo That is not a number of minutes, so 5 is used.
  set "MINUTES=5"
  set "WAIT=300"
  goto :minutesok
)
:cutzero
if "%MINUTES:~0,1%"=="0" if not "%MINUTES:~1%"=="" (
  set "MINUTES=%MINUTES:~1%"
  goto :cutzero
)
rem more than four digits is far too many (padded first, so that cutting a piece off a short number never reaches past its end)
set "PAD=%MINUTES%xxxx"
if not "%PAD:~4,1%"=="x" set "MINUTES=999"
set "WAIT=0"
set /a WAIT=%MINUTES%*60 >nul 2>&1
if %WAIT% LSS 60 (
  echo That is not a number of minutes, so 5 is used.
  set "MINUTES=5"
  set "WAIT=300"
)
if %WAIT% GTR 3600 (
  echo The wait can be 1 to 60 minutes, so 60 is used.
  set "MINUTES=60"
  set "WAIT=3600"
)
:minutesok

rem Windows only runs screen savers whose name ends in .scr. The screen saver is Meridian.exe under that name,
rem so it always matches the program, and the themes folder next to it is found as usual.
copy /y "Meridian.exe" "Meridian.scr" >nul
if errorlevel 1 (
  echo Could not create Meridian.scr in this folder. Is the folder read-only, or the screen saver on the screen right now?
  echo.
  pause
  exit /b 1
)
rem a copy of a downloaded file can carry the "from the internet" mark, which could make Windows refuse to start it on its own
powershell -NoProfile -ExecutionPolicy Bypass -Command "Unblock-File -LiteralPath 'Meridian.scr'" >nul 2>&1

rem Remember what was set before, so Remove-ScreenSaver.bat can put it back. Only the first time: running this again with another number
rem (or from another folder) must not save Meridian's own setting as "what was there before".
set "CUR="
set "CURTYPE=REG_SZ"
rem (a screen saver can be stored as REG_SZ or, with a variable such as SystemRoot in it, as REG_EXPAND_SZ: both are remembered, with their type)
for /f "tokens=2,*" %%A in ('reg query "HKCU\Control Panel\Desktop" /v SCRNSAVE.EXE 2^>nul') do (
  if /i "%%A"=="REG_SZ" set "CUR=%%B"
  if /i "%%A"=="REG_EXPAND_SZ" set "CUR=%%B"
  if /i "%%A"=="REG_EXPAND_SZ" set "CURTYPE=REG_EXPAND_SZ"
)
set "CURNAME="
for %%F in ("%CUR%") do set "CURNAME=%%~nxF"
if /i "%CURNAME%"=="Meridian.scr" goto :backedup
set "OLDACT="
for /f "tokens=2,*" %%A in ('reg query "HKCU\Control Panel\Desktop" /v ScreenSaveActive 2^>nul') do if /i "%%A"=="REG_SZ" set "OLDACT=%%B"
set "OLDWAIT="
for /f "tokens=2,*" %%A in ('reg query "HKCU\Control Panel\Desktop" /v ScreenSaveTimeOut 2^>nul') do if /i "%%A"=="REG_SZ" set "OLDWAIT=%%B"
reg delete "HKCU\Software\Meridian\ScreenSaverBackup" /f >nul 2>&1
reg add "HKCU\Software\Meridian\ScreenSaverBackup" /v Saved /t REG_SZ /d 1 /f >nul 2>&1
if defined CUR reg add "HKCU\Software\Meridian\ScreenSaverBackup" /v Scr /t REG_SZ /d "%CUR%" /f >nul 2>&1
if defined CUR reg add "HKCU\Software\Meridian\ScreenSaverBackup" /v ScrType /t REG_SZ /d "%CURTYPE%" /f >nul 2>&1
if defined OLDACT reg add "HKCU\Software\Meridian\ScreenSaverBackup" /v Active /t REG_SZ /d "%OLDACT%" /f >nul 2>&1
if defined OLDWAIT reg add "HKCU\Software\Meridian\ScreenSaverBackup" /v Wait /t REG_SZ /d "%OLDWAIT%" /f >nul 2>&1
:backedup

rem Tell Windows (only for you: nothing here needs administrator rights)
reg add "HKCU\Control Panel\Desktop" /v SCRNSAVE.EXE /t REG_SZ /d "%~dp0Meridian.scr" /f >nul
if errorlevel 1 goto :failed
reg add "HKCU\Control Panel\Desktop" /v ScreenSaveActive /t REG_SZ /d 1 /f >nul
if errorlevel 1 goto :failed
reg add "HKCU\Control Panel\Desktop" /v ScreenSaveTimeOut /t REG_SZ /d %WAIT% /f >nul
if errorlevel 1 goto :failed
rundll32.exe user32.dll,UpdatePerUserSystemParameters 1, True

echo.
echo   Done. Windows will start the Meridian screen saver after %MINUTES% minutes without keyboard or mouse use.
echo   Any key, click or mouse move closes it.
echo.
echo   - Try it now:  double-click Meridian.scr  (or right-click it and choose Test)
echo   - Keep this folder where it is. If you move it, run this file again.
echo   - Wait time:   run this file again with a number (1 to 60), for example   Install-ScreenSaver.bat 10
echo   - Sleep too:   in Meridian, Settings, Away, can put the PC to sleep after Meridian has been on for a while
echo   - Remove it:   run Remove-ScreenSaver.bat  (it puts your earlier screen saver back)
echo   - If it never starts, sign out of Windows and in again once.
echo.
pause
exit /b 0

:failed
echo.
echo Windows would not take the setting. You can still pick it by hand: right-click Meridian.scr and choose Install.
echo.
pause
exit /b 1
