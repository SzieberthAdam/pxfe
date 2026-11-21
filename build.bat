@echo off
rem Builds pxfe.exe (and the core unit tests) with the w64devkit GCC bundled with raylib (C:\raylib).
rem   build.bat         - build pxfe.exe
rem   build.bat test    - build and run the quick core tests (test_pxfont, test_outline, test_bmsrc, test_fit, test_sheet, test_fsettings,
rem                       test_pxtf with library index and loader, test_store, test_package, test_selection, test_raster, test_colormath)
rem   build.bat libtest [root] [--quick] - build and run tests\test_library.exe: round trip of a whole real library (text files and
rem                       store), the store built from the files against the real pxfdb, timings, similarity (slow: minutes)
rem   build.bat debug   - build pxfe.exe with a console window and -g
rem   build.bat e2e     - build tests\tmp\pxfe_e2e.exe (console, -g) for tests\e2e\run_all.bat; leaves pxfe.exe alone
cd /d "%~dp0"
set PATH=C:\raylib\w64devkit\bin;%PATH%
set RL=C:\raylib\raylib\src
set CFLAGS=-std=gnu99 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -I src -I vendor -I vendor/sqlite3 -I %RL%
set SQL=vendor\sqlite3\libsqlite3.a
set ZLIB=vendor\zlib\adler32.c vendor\zlib\crc32.c vendor\zlib\deflate.c vendor\zlib\inffast.c vendor\zlib\inflate.c vendor\zlib\inftrees.c vendor\zlib\trees.c vendor\zlib\zutil.c
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
gcc src\*.c %ZLIB% %SQL% -o %OUT% %MODE% %CFLAGS% %LIBS%
if errorlevel 1 (echo Build failed & exit /b 1)
echo Build OK: %OUT%
exit /b 0

:test
set CORE=src\pxfont.c src\pxtf.c src\store.c src\store_sim.c src\library.c
gcc tests\test_pxfont.c src\pxfont.c src\pxtf.c -o tests\test_pxfont.exe -O0 -g %CFLAGS%
if errorlevel 1 (echo Test build failed & exit /b 1)
gcc tests\test_pxtf.c %CORE% %ZLIB% %SQL% -o tests\test_pxtf.exe -O0 -g %CFLAGS%
if errorlevel 1 (echo Test build failed & exit /b 1)
gcc tests\test_store.c %CORE% %ZLIB% %SQL% -o tests\test_store.exe -O0 -g %CFLAGS%
if errorlevel 1 (echo Test build failed & exit /b 1)
gcc tests\test_package.c %CORE% %ZLIB% %SQL% -o tests\test_package.exe -O0 -g %CFLAGS%
if errorlevel 1 (echo Test build failed & exit /b 1)
gcc tests\test_outline.c src\outline.c -o tests\test_outline.exe -O1 -g %CFLAGS%
if errorlevel 1 (echo Test build failed & exit /b 1)
gcc tests\test_bmsrc.c src\bmsrc.c -o tests\test_bmsrc.exe -O1 -g %CFLAGS%
if errorlevel 1 (echo Test build failed & exit /b 1)
gcc tests\test_fsettings.c src\fsettings.c %SQL% -o tests\test_fsettings.exe -O0 -g %CFLAGS%
if errorlevel 1 (echo Test build failed & exit /b 1)
gcc tests\test_fit.c src\fit.c src\outline.c -o tests\test_fit.exe -O1 -g %CFLAGS%
if errorlevel 1 (echo Test build failed & exit /b 1)
gcc tests\test_sheet.c src\sheet.c src\sheetref.c src\fit.c src\outline.c src\bmsrc.c -o tests\test_sheet.exe -O1 -g %CFLAGS%
if errorlevel 1 (echo Test build failed & exit /b 1)
gcc tests\test_selection.c src\selection.c -o tests\test_selection.exe -O1 -g %CFLAGS%
if errorlevel 1 (echo Test build failed & exit /b 1)
gcc tests\test_raster.c src\raster.c -o tests\test_raster.exe -O1 -g %CFLAGS%
if errorlevel 1 (echo Test build failed & exit /b 1)
gcc tests\test_colormath.c src\colormath.c -o tests\test_colormath.exe -O1 -g %CFLAGS%
if errorlevel 1 (echo Test build failed & exit /b 1)
tests\test_colormath.exe
if errorlevel 1 exit /b 1
tests\test_pxfont.exe
if errorlevel 1 exit /b 1
tests\test_selection.exe
if errorlevel 1 exit /b 1
tests\test_raster.exe
if errorlevel 1 exit /b 1
tests\test_outline.exe
if errorlevel 1 exit /b 1
tests\test_bmsrc.exe
if errorlevel 1 exit /b 1
tests\test_fit.exe
if errorlevel 1 exit /b 1
tests\test_sheet.exe
if errorlevel 1 exit /b 1
tests\test_fsettings.exe
if errorlevel 1 exit /b 1
tests\test_pxtf.exe
if errorlevel 1 exit /b 1
tests\test_store.exe
if errorlevel 1 exit /b 1
tests\test_package.exe
exit /b %errorlevel%

:libtest
set CORE=src\pxfont.c src\pxtf.c src\store.c src\store_sim.c src\library.c
gcc tests\test_library.c %CORE% %ZLIB% %SQL% -o tests\test_library.exe -O2 %CFLAGS%
if errorlevel 1 (echo Test build failed & exit /b 1)
tests\test_library.exe %2 %3
exit /b %errorlevel%
