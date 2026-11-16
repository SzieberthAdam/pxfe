@echo off
setlocal enabledelayedexpansion
rem Runs every end-to-end script of tests\e2e against a console build of pxfe (see README.md in this folder).
rem Exit code 0 when all scripts passed, 1 when any assertion failed, 2 when the setup failed.
cd /d "%~dp0\..\.."

call "%CD%\build.bat" e2e
if errorlevel 1 exit /b 2
powershell -NoProfile -ExecutionPolicy Bypass -File tests\e2e\prep_real.ps1
if errorlevel 1 exit /b 2

set EXE=tests\tmp\pxfe_e2e.exe
set OPTS=--ini tests\tmp\e2e.ini
set LIBD=tests\tmp\e2elib
set DB=tests\tmp\e2e.pxfdb
set H1=0820dc69a21b1cdb3a44d2cff7d884d8
set H2=e6109c134eda8d747bac9a3c2a62d204
set H3=03696f658319e9dfddc2cb6ea2520592
set H4=fa96e3583da8e003fc25404ac00f764e
set HTF=b925951bb94de1d15ec27404b3261476
set REALH=b86206e5d4408e60dd8174f29050daa7
set REALD=tests\tmp\reallib
set FAILS=0
set RUNS=0
if not exist tests\tmp\shots mkdir tests\tmp\shots
if not exist tests\tmp\e2e_out mkdir tests\tmp\e2e_out
if not exist tests\tmp\fl mkdir tests\tmp\fl

call :fresh
call :run 00_smoke "%DB% --font %H1%"
call :run 01_grid "%REALD% --import-to tests\tmp\real1.pxfdb --font %REALH%"
call :fresh
call :run 02_metrics "%DB% --font %H1%"
call :fresh
call :run 03_selection "%DB% --font %H1%"
call :fresh
call :run 04_delete "%DB% --font %H1%"
call :fresh
call :run 05_clone "%DB% --font %H1%"
call :fresh
call :run 06_editor "%DB% --font %H1%"
call :fresh
call :run 06b_palette "%DB% --font %H3%"
call :fresh
call :run 06c_noascent "%DB% --font %H2%"
call :fresh
call :run 07a_save "%DB% --typeface %HTF%"
call :run 07b_reload "%DB%"
call :fresh
call :run 07d_real "%REALD% --import-to tests\tmp\real2.pxfdb --font %REALH%"
call :fresh
call :run 07e_nodb ""
call :fresh
call :run 08a_quit_prompt "%DB% --font %H1%"
call :fresh
call :run 08b_quit_save "%DB% --font %H1%"
call :run 08c_quit_check "%DB%"
call :fresh
call :run 08d_quit_clean "%DB% --font %H1%"
call :fresh
call :run 08e_load_prompt "%DB% --font %H1%"
call :fresh
call :run 09_misc "%DB% --font %H1%"
call :fresh
call :run 10_readonly "%DB% --typeface %HTF%"
call :fresh
call :run 11_notmodified "%DB% --font %H1%"
call :fresh
call :run 12_typeface "%DB% --typeface %HTF%"
call :fresh
call :run 13_default_kern "%DB% --font %H1%"
call :fresh
call :run 14_browser "%DB%"
call :fresh
call :run 15_import "%DB%"
call :fresh
call :run 16_load "%DB%"
call :fresh
call :run 17_save "%DB%"
call :run 17b_reload "%DB%"
call :fresh
call :run 18_saveas "%DB%"
call :run 18b_reload_saveas "tests\tmp\e2e_copy.pxfdb"
call :fresh
call :run 19_uncoded "%DB% --font %H4%"
call :fresh
call :run 21_usage_finder "%DB%"
call :fresh
call :run 23_modified_prompts "%DB% --font %H1%"
call :fresh
call :run 24_import_flow ""
call :fresh
call :run 25_export_empty "%DB%"
call :fresh
call :run 26_export_append "%DB% --font %H1%"
call :fresh
call :run 27_export_overwrite "%DB%"
call :fresh
call :run 28_export_nobackup "%DB%"
call :fresh
call :run 29_edit_replace "%DB% --font %H1%"
call :fresh
call :run 31_edit_twice "%DB% --font %H1%"
call :fresh
call :run 32_edit_back_to_original "%DB% --font %H1%"
call :fresh
call :run 30_delete_save "%DB%"
call :run 30b_delete_reload "%DB%"
call :fresh
call :run 40_glyph_bar "%DB% --font %H1%"
call :fresh
call :run 41_glyph_panel "%DB% --font %H1%"
call :fresh
call :run 42_layers_add "%DB% --font %H1%"
call :fresh
call :run 43_layers_edit "%DB% --font %H1%"
call :fresh
call :run 44_layers_pixels "%DB% --font %H1%"
call :fresh
call :run 45_layers_discover "%DB% --font %H1%"
call :fresh
call :run 50_sample_browser "%DB%"
call :fresh
call :run 51_sample_strip "%DB% --font %H1%"
call :run 53_sample_persist "%DB% --font %H1%"
call :fresh
call :run 52_sample_strip_live "%DB% --font %H1%"
call :fresh
call :run 52b_glyph_preview "%DB% --font %H1%"
call :fresh
call :run 54_typeface_boxes "%DB%"
call :fresh
call :run 55_delete_requests "%DB%"
call :fresh
call :run 58_delete_ui "%DB%"
call :fresh
call :run 59_navigation "%DB%"
call :fresh
call :run 59b_navigation_orphan "%DB% --font %H3%"
call :fresh
(echo [browser]& echo tab=1& echo sidetab=1) > tests\tmp\e2e.ini
call :run 61_views "%DB%"
call :fresh
call :run 70_unifont "%DB% --font %H1%"
call :run 93_menu_keys "%DB%"
call :fresh
call :run 94_font_layer_smoke "%DB%"
call :fresh
call :run 95_fl_newfont "%DB%"
call :fresh
call :run 96_fl_adjust "%DB% --font %H1%"
call :fresh
call :run 97_fl_save "%DB% --font %H1%"
call :run 97b_fl_reload "%DB% --font %H1%"
call :fresh
call :run 97c_fl_empty "%DB% --font %H1%"
call :fresh
call :run 97d_fl_saveas "%DB% --font %H1%"
call :fresh
call :run 98_fl_locked "%DB% --font %H1%"
call :fresh
call :run 99_fl_edit_commit "%DB% --font %H1%"
call :fresh
call :run 101_import_font "%DB%"
call :fresh
call :run 102_import_glyph "%DB%"
call :run 102b_import_reload "%DB%"
call :fresh
call :run 104_default_glyph "%DB% --font %H1%"
call :run 104b_default_reload "%DB% --font 897787da6f47a5d4ad8068bf5bc1bab8"
call :fresh
call :run 105_lock "%DB% --font %H1%"
call :run 105b_lock_reload "%DB% --font 36e0649997646b5da1e22ded4ab8f8db"
call :fresh
call :run 106_import_sheet "%DB%"

echo.
echo %RUNS% scripts run, %FAILS% failed
if %FAILS% gtr 0 exit /b 1
exit /b 0

:fresh
if exist tests\tmp\e2elib rmdir /s /q tests\tmp\e2elib
xcopy /e /i /q /y testdata\lib tests\tmp\e2elib >nul
copy /y testdata\lib.pxfdb tests\tmp\e2e.pxfdb >nul
if exist tests\tmp\xout rmdir /s /q tests\tmp\xout
if exist tests\tmp\e2e_copy.pxfdb del tests\tmp\e2e_copy.pxfdb
if exist tests\tmp\e2e.pxfdb-settings del tests\tmp\e2e.pxfdb-settings
if exist tests\tmp\e2e_copy.pxfdb-settings del tests\tmp\e2e_copy.pxfdb-settings
if exist tests\tmp\e2e_imp.pxfdb-settings del tests\tmp\e2e_imp.pxfdb-settings
if exist tests\tmp\e2e_settings_before del tests\tmp\e2e_settings_before
if exist tests\tmp\e2e.ini del tests\tmp\e2e.ini
exit /b 0

:run
set /a RUNS+=1
set NAME=%~1
%EXE% %~2 %OPTS% --play tests\e2e\%NAME%.txt > tests\tmp\e2e_out\%NAME%.log 2>&1
set RC=!errorlevel!
findstr /b /c:"FAIL" /c:"RESULT" tests\tmp\e2e_out\%NAME%.log
if not "!RC!"=="0" (
  set /a FAILS+=1
  echo   ^> %NAME% exited with code !RC!, see tests\tmp\e2e_out\%NAME%.log
)
exit /b 0
