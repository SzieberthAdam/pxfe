@echo off
rem Builds pxfe.exe (and the core unit tests) with the w64devkit GCC bundled with raylib (C:\raylib).
rem   build.bat         - build pxfe.exe
rem   build.bat test    - build and run the quick core tests (test_pxfont, test_pxtf with library index and loader, test_store)
rem   build.bat libtest [root] [--quick] - build and run tests\test_library.exe: round trip of a whole real library (text files and
rem                       store), the store built from the files against the real pxfdb, timings, similarity (slow: minutes)
rem   build.bat debug   - build pxfe.exe with a console window and -g
rem   build.bat e2e     - build tests\tmp\pxfe_e2e.exe (console, -g) for tests\e2e\run_all.bat; leaves pxfe.exe alone
cd /d "%~dp0"
set PATH=C:\raylib\w64devkit\bin;%PATH%
set RL=C:\raylib\raylib\src
set CFLAGS=-std=gnu99 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -I src -I vendor -I vendor/sqlite3 -I %RL%
set SQL=vendor\sqlite3\libsqlite3.a
set LIBS=-L %RL% -lraylib -lopengl32 -lgdi32 -lwinmm -lcomdlg32 -lole32

if "%1"=="test" goto test
if "%1"=="libtest" goto libtest

set OUT=pxfe.exe
set MODE=-O2 -mwindows
if "%1"=="debug" set MODE=-O0 -g
if "%1"=="e2e" (
  if not exist tests\tmp mkdir tests\tmp
  set OUT=tests\tmp\pxfe_e2e.exe
  set MODE=-O1 -g -DGetCharPressed=Play_GetChar
)
gcc src\*.c %SQL% -o %OUT% %MODE% %CFLAGS% %LIBS%
if errorlevel 1 (echo Build failed & exit /b 1)
echo Build OK: %OUT%
exit /b 0

:test
set CORE=src\pxfont.c src\pxtf.c src\store.c src\store_sim.c src\library.c
gcc tests\test_pxfont.c src\pxfont.c src\pxtf.c -o tests\test_pxfont.exe -O0 -g %CFLAGS%
if errorlevel 1 (echo Test build failed & exit /b 1)
gcc tests\test_pxtf.c %CORE% %SQL% -o tests\test_pxtf.exe -O0 -g %CFLAGS%
if errorlevel 1 (echo Test build failed & exit /b 1)
gcc tests\test_store.c %CORE% %SQL% -o tests\test_store.exe -O0 -g %CFLAGS%
if errorlevel 1 (echo Test build failed & exit /b 1)
tests\test_pxfont.exe
if errorlevel 1 exit /b 1
tests\test_pxtf.exe
if errorlevel 1 exit /b 1
tests\test_store.exe
exit /b %errorlevel%

:libtest
set CORE=src\pxfont.c src\pxtf.c src\store.c src\store_sim.c src\library.c
gcc tests\test_library.c %CORE% %SQL% -o tests\test_library.exe -O2 %CFLAGS%
if errorlevel 1 (echo Test build failed & exit /b 1)
tests\test_library.exe %2 %3
exit /b %errorlevel%
