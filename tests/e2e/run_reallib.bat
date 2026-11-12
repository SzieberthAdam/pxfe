@echo off
rem Smoke test on the real library (slow): tests\e2e\20_reallib_smoke.txt on pixelglyphlib, loaded from its pxfdb.pxfdb and imported from its folder.
rem Nothing is written into pixelglyphlib: the database is loaded read-only (the script's Save As goes to tests\tmp\real_saveas.pxfdb), the folder
rem is imported into tests\tmp\real.pxfdb.  PXFE_TRACE prints the timings (TRACE load / import / saveas).
rem   run_reallib.bat          both
rem   run_reallib.bat import   the folder only (minutes)
rem   run_reallib.bat db       the pxfdb only
rem Exit code 0 when it passed, 1 when an assertion failed, 2 when the setup failed.
cd /d "%~dp0\..\.."
set LIBREAL=C:\Users\jnsGEzD3\Documents\GitHub\pixelglyphlib
if not exist "%LIBREAL%\pxfm" (echo real library not found: %LIBREAL% & exit /b 2)
call "%CD%\build.bat" e2e
if errorlevel 1 exit /b 2
if not exist tests\tmp\shots mkdir tests\tmp\shots
if not exist tests\tmp\e2e_out mkdir tests\tmp\e2e_out
set PXFE_TRACE=1
set FAILS=0
if /i not "%1"=="import" call :one db "%LIBREAL%\pxfdb.pxfdb"
if /i not "%1"=="db" call :one import "%LIBREAL% --import-to tests\tmp\real.pxfdb"
exit /b %FAILS%

:one
if exist tests\tmp\e2e_real.ini del tests\tmp\e2e_real.ini
tests\tmp\pxfe_e2e.exe %~2 --ini tests\tmp\e2e_real.ini --play tests\e2e\20_reallib_smoke.txt > tests\tmp\e2e_out\20_reallib_%1.log 2>&1
set RC=%errorlevel%
echo [%1]
findstr /b /c:"TRACE" /c:"FAIL" /c:"RESULT" tests\tmp\e2e_out\20_reallib_%1.log
if not "%RC%"=="0" set FAILS=1
exit /b 0
