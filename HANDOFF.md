# pxfe handoff (2026-11-13)

Versions 5 to 9 are implemented: Load, Save, Import, Export (5), navigation (6), views (7), the design system with Noto Sans and GNU Unifont (8), and font layers (9).
The design is in [DESIGN.md](DESIGN.md), one section per version; "Version 5: Load, Save, Import, Export" replaces the storage tiers, the save pipeline and the drafts of version 4.
pxfe is meant to run on Windows, macOS and Linux; today it builds on Windows only (`build.bat`, `vendor/sqlite3/libsqlite3.a`, `src/winfile.c`).
The files of `handoff/` are the reports of the version 4 agents, kept for history.

## State

- `build.bat e2e` builds with no warnings.
- `build.bat test` passes: 136 pxfont, 248 outline, 143 bmsrc, 102 fsettings, 101 pxtf and 162 store checks, 0 failed.
- `tests\e2e\run_all.bat` passes: 71 scripts, 0 failed (version 9 added 95 to 99, 97b, 97c and 97d).
- `tests\e2e\run_reallib.bat` loads `pixelglyphlib\pxfdb.pxfdb` read-only and imports the folder into `tests\tmp\real.pxfdb`; see the report of the last run for its timings.
- Plain `build.bat` fails only at the link step when a running `pxfe.exe` locks the file; closing it is enough.

## What version 5 changed

- `src/store.c`: the store is an in-memory database that Load fills from a `.pxfdb`; Save writes a temp file with `VACUUM INTO` and replaces the file; a change counter tells whether memory differs from the file; Import and Export work on folders; the tier bookkeeping is gone.
- `src/main.c`: the File menu (Load, Save, Save As, Import Folder, Export to Folder, Quit), the Save button, the window title and status bar markers, the prompts (Save / Discard / Cancel), the export dialog (Append, Overwrite, backup), the commit of a duplicate into memory when its font view is left.
- `src/storebar.*`, the pipeline, the Drafts tab and the draft code paths of the application are removed.
  `px_save_draft`, `px_load_draft` and the draft scan of `library.c` stay in the core, unused and still tested by `test_pxtf.c`.
- `src/editor.c`: the glyph view's preview strip draws the shared multi-line sample (`src/sample.h`) at 1x and 2x, as many lines as fit, and the edited glyph repeated after the last line shown.
- The end-to-end scripts are migrated; the new ones are 15 to 18b, 23 to 31 and 52b.

## Things to know

- Save runs on the UI thread (`VACUUM INTO`); on the real library it freezes the window for the length of `TRACE saveas`.
- Replacing a duplicate's earlier commit removes a font or typeface only when that commit created it (an original of the load never goes), no other left duplicate's current commit names it and, for a font, no remaining typeface names it; glyphs go only when no remaining font names them.
  The list of left duplicates (`liveDups` in `src/main.c`, at most 64, cleared when another database is installed) passes the ownership of a shared font to the duplicate that still holds it.
- SQLite gets Windows ANSI paths, as before; a path with characters outside the code page is not supported.
- The scripts run fast: two clicks on one spot within 0.4 s are a double click, and a click right after a layout change needs `wait 6` (see `tests/e2e/README.md`).
- The real library `C:\Users\jnsGEzD3\Documents\GitHub\pixelglyphlib` is never written by pxfe's tests.
  It holds two user-made fonts of 04b03 (2 pxfm and 2 pxtf files) that its `pxfdb.pxfdb` lacks, so importing the folder gives a database with more fonts than the file.
- The agents work only on copies under `tests/tmp/` and on `testdata/`.

## Version 6: navigation

The top bar's "File" button is "Menu", and the segmented control is Typefaces | Fonts | Glyphs, which turns into typeface | style | glyph inside a typeface, a font and a glyph.
The design is in the last section of [DESIGN.md](DESIGN.md), "Version 6: navigation".

- `src/main.c`: `NavView`, `DrawNav`, `NavBack` (Alt+Left, one level up), `ShowTypefaces`, `ShowFonts`, `ShowTypeface`, `ShowGlyphs`, `ShowFont`, `CloseGlyphView`.
- `src/browser.c`: the tab row is gone; the Typeface view (`tview_ui`, `Browser_OpenTypeface`, `Browser_CloseTypeface`) is the Typefaces tab narrowed to one typeface.
- `src/play.inc`: `nav` (0 Typefaces, 1 Typeface, 2 Fonts, 3 Font, 4 Glyph, 5 Glyphs), `navactive`, `intypeface`, `assert navseg <0-2> <text>`; the segments are `@nav.0` to `@nav.2`, and `@Typefaces`, `@Fonts`, `@Glyphs` in the list views.
- The scripts use `@Menu` for the menu, `@nav.0` plus `@Typefaces` for the old `@Library`, and `key right alt` for the old `@tab.font`.
- New scripts: `59_navigation` (all transitions) and `59b_navigation_orphan` (a font without a typeface: "Orphaned").
- `tests\e2e\60_shots_nav.txt` takes the six screenshots (`tests\tmp\nav\1_typefaces.png` to `6_glyphs.png`) on `tests\tmp\nav\real.pxfdb`, a copy of the real library; it is not in `run_all.bat`.

## Version 7: views

The design is in the last section of [DESIGN.md](DESIGN.md), "Version 7: views".
The program starts in the Typefaces view, every zoom is one stepper, the Glyphs view has a side panel View | Usage, and the Typeface tab moved to the Typefaces and Typeface views.

- `src/theme.c`: `Th_Stepper` (the zoom control `[-] 3x [+]`, `Th_StepperHover` for hints) and `Th_Section`.
  Users: the toolbar and the glyph bar, the sample strip, the browser's View tab, the Glyphs view's View tab.
- `src/finder.c`: no header and no toolbar; `draw_view_tab`, the tabs `fside` (View, Usage), default zoom 3, no Auto, `Finder_SelectedGlyph`.
- `src/main.c`: `HandleInsightTarget` opens the Glyph view when the font was chosen in the Glyphs view; a load shows `ShowTypefaces`; the typeface snapshots of the undo stack are removed.
- `src/panel.inc`: the font panel is Font | Glyph | Similar (`panelTab` 0 to 2, ini key `fontTab`); `DrawTypefaceTab` is the read-only Typeface tab, called by `browser.c` through `Browser_SetTypefaceTab`.
- `src/browser.c`: `SIDE_VIEW`, `SIDE_SIMILAR`, `SIDE_TYPEFACE` (shown View | Typeface | Similar in the Typefaces and Typeface views), `sel_typeface`; `tf_info` is removed.
- Typeface editing (the user's decision): "Duplicate Typeface to Edit" (`BROWSER_DUPLICATE_TYPEFACE`, in the Typeface tab and in the Typeface view's heading) opens the font of the selected variant, duplicates it and shows the Typeface view again; `TypefaceEditing` (panel.inc) says that the open duplicate is that typeface, and `DrawTypefaceTab` then edits `cont` (the old field code, without undo snapshots).
  The commit is the existing duplicate commit (`CommitDuplicate`); it now keeps the ownership of what its earlier commit created when the new commit names the same font or typeface again.
  `ctFocus` is back, the browser does not read keys or characters while a field has the keyboard (`extTyping`).
  `12_typeface`, `07a_save` and `07b_reload` test it.
- New: `tests/e2e/61_views.txt` (in `run_all.bat`, with an ini that remembers the Fonts tab), `62_shots_views.txt` (screenshots on a copy of the real library, `tests\tmp\p1`; not in `run_all.bat`).
- Script 28 failed once in a full run and passed alone and in the next full run (a timing flake).

## Version 8: design system

The design is in the last section of [DESIGN.md](DESIGN.md), "Version 8: design system": the type scale, the component catalogue, the "What changed visibly" list and the owner table (component, function, file).
The rule of the round: one component is one function in `src/theme.c`, and a view never draws a second variant.

- The last components moved in this step: toast (`Th_Toast`, `Th_ToastDraw`), the raised surface (`Th_Raised`), the menu (`Th_Menu`, `Th_MenuRect`) and the status bar (`Th_StatusBar`).
- Menus: the Menu button, the "..." menu, the glyph grid's context menu, the library views' context menu and the Style and Format dropdown lists are all `Th_Menu`.
  The Add Layer popover, the all-characters popup, the dialogs and the toast use `Th_Raised`.
- Status bar: `main.c` and the glyph view (`editor.c`) both call `Th_StatusBar` with their own parts.
  The library views' own bar was drawn over by the application's bar, so it is deleted.
- `Th_FitText` also takes `TH_F_TAIL` (keeps the end of a path), and its ellipsis, which was a mis-encoded string, is right now.
- Shot scripts (not in `run_all.bat`, all run and report 0 failed): 60 (navigation), 62 (version 7 views), 64 to 68 (one component each: hash, buttons, fields, fields on the test library, lists), 69 (the final shots).
  63 is deleted: 69 replaces it.
  Their folders are `tests\tmp\nav`, `tests\tmp\p1`, `tests\tmp\p2\c1` to `c4`; 69 writes `tests\tmp\p2\final` at 1400 x 900 and, prefixed `s_`, at 1110 x 600.
  The shots use a copy of the executable (`tests\tmp\p2\pxfe_p2.exe`) so that a build is never blocked by a running window.
- Duplicate to Edit is one primary button (`Th_Duplicate`, `Th_DuplicateW`, `Th_DupLabel` in `theme.h`) at every site: info bar, glyph bar (a plain "Read-only" label and the button, script name `ro_chip`), library cards, rows and selection blocks, the Typeface view (heading, font rows) and the Typeface tab.
  Script 40 now asserts `w.Duplicate_to_Edit` 1 beside `ro_chip` in the original's glyph view.
- The status bar draws its hash through `Th_HashShort` with the new flag `TH_HASH_SMALL` (small kind word, the hash in Unifont on the baseline of the neighbours).
- Menus take the keyboard in `Th_Menu` (Up, Down, Home, End, Enter, Escape; the highlight is shared with the mouse hover; a scrolled list keeps it in view).
  While a menu is open those keys do not reach shortcuts or views: `IsKeyPressed` and `IsKeyPressedRepeat` are wrapped in `theme.h` (`Th_MenuKeys`), and `Th_Menu` calls the raw functions.
  `Th_MenuHi` and the play assert `menuhi N` expose the highlight.
- Deleted: `Th_Panel` (unused) and the raygui style plumbing (no raygui widget is used).
- User interface files: 10947 lines in all (10960 before this step, 13 fewer): main.c 2729, panel.inc 344, gpanel.inc 90, browser.c 2206, finder.c 693, editor.c 1109, layers.c 730, insight.c 973, sample.c 768, theme.c 1003, theme.h 302.
- Tests: `build.bat test` 136 pxfont, 101 pxtf and 162 store checks, 0 failed; `tests\e2e\run_all.bat` 61 scripts (new: 93_menu_keys, 21 asserts), 0 failed; `build.bat` and `build.bat e2e` build without warnings.
- Final screenshots: `tests\tmp\p3\final` (script 69, run with a copy of the exe from the repo root).

### Fonts

- The interface no longer uses system fonts.
  Noto Sans v2.015 (`assets/fonts/noto-sans`, OFL 1.1) draws all interface text at three sizes only: heading 18 (SemiBold), body 16 (Regular), caption and small 13.
- GNU Unifont 18.0.01 (`assets/fonts/unifont`, OFL 1.1 or GPLv2+ with the font exception) draws hashes, codes and glyph captions, always at 16 px (32 px for the Glyphs preview), point-filtered and on whole pixels.
- Unifont glyphs of any plane are rasterised on demand into a 1024 x 1024 atlas that is flushed and refilled when full, so no glyph is ever dropped (`theme.c`).
- A glyph caption shows the character in Unifont and switches to `U+XXXX` on hover, both centred; a code point without a visible Unifont image always shows `U+XXXX`.
- The fonts are looked up in `assets/fonts` beside the exe, then two folders up (for `tests\tmp\pxfe_e2e.exe`), then in the working folder; a missing file raises an error toast and the default font stands in.
- Script `70_unifont` checks the hover switch and code points of several planes, also after a forced atlas flush.

### Line endings

- Every text file is LF, in the repository and in every checkout; only the `.bat` files are CRLF on disk, because cmd needs it.
- `.gitattributes` enforces this; fonts, images, the SQLite library and `.pxfdb` are binary, and the hash-named `.pxgl`, `.pxfm` and `.pxtf` files are never converted.
- An editor or a `sed` that writes CRLF into a source file is undone by Git at the next add; a `.bat` saved as LF breaks `call :label`.

## Version 9: font layers

The design is in the last section of [DESIGN.md](DESIGN.md), "Version 9: font layers".
A font layer belongs to a font: another font's drawing of the same code point under every glyph, from a database font or a font file.
It is saved with the font in a sidecar next to the database.

- New files: `src/flayers.c` and `.h` (the font layer: source, state, glyph cache, corrections, `FlDoc`), `src/fsettings.c` and `.h` (the sidecar `<db>-settings`, a SQLite file with the tables `layer` and `correction`, keyed by pxfm hash), `src/outline.c` and `.h` (outline reader and rasteriser: TrueType, CFF, WOFF, EOT, Type 1 PFB, TTC; its own inflate, WOFF2 is not supported), `src/bmsrc.c`, `src/bmsrc.h` and `src/bmsrc_cp.inc` (bitmap sources: BDF, PCF, FNT, FON, image sheets with a JSON descriptor, bitmap strikes of ttf and otf).
- Changed: `src/layers.c` and `.h` (the Layers tab and the Add Layer popover with the File source), `src/main.c` (Menu, New Font; the document, `FlSync`, `SaveSidecar`), `src/editor.c`, `src/play.inc`, `src/theme.c` and `.h`, `src/browser.c`.
- Behaviours: Save writes the sidecar (alone when only the settings changed); Save As writes a new one, the old stays; Import starts empty; Export does not touch it; with no layers nothing is written and an old sidecar is deleted.
  An original may get layers, the `.pxfdb` stays byte for byte as it was (script 98).
  A duplicate shares the original's hash until its first pixel edit, so a layer added before the edit is also the original's; after the edit the layers are written under the new hash at the commit or at Save, and the original's stay.
- New Font (Menu, first item) makes an empty orphan font; Add Glyph in the font view adds a glyph.
- Bug found by the scripts and fixed: the browser blurred every focused text field of a dialog drawn over it, so typing in the New Font dialog did nothing (`src/browser.c`, `Th_TextIsIn` in `src/theme.c`).
- `build.bat test` now also runs `tests\test_bmsrc.exe`; a typo `exit \b` in the bmsrc test build line is fixed.

How to run:

- `tests\e2e\run_all.bat` runs everything; a single script is started like the others, for example `tests\tmp\pxfe_e2e.exe tests\tmp\e2e.pxfdb --font 0820dc69a21b1cdb3a44d2cff7d884d8 --duplicate --ini tests\tmp\e2e.ini --play tests\e2e\98_fl_orig.txt` after copying `testdata\lib.pxfdb` to `tests\tmp\e2e.pxfdb`.
- The scripts and their commands are in [tests/e2e/README.md](tests/e2e/README.md): 94 smoke, 95 sources and New Font, 96 adjustments and corrections, 97 to 97d the sidecar, 98 originals, 99 duplicate and commit; asserts per script 17, 58, 57, 28, 25, 14, 24, 27 and 26.
- `tests\e2e\100_shots_fl.txt` (not in `run_all.bat`) takes the screenshots in `tests\tmp\fl` at 1400 x 900.

State: `build.bat test` and `build.bat` (no warnings) pass, `run_all.bat` 71 scripts, 0 failed.

Licence record of what the version added:

- `vendor/stb_truetype.h`: v1.26, public domain (alternatively MIT), Sean Barrett.
  Used by the tests only (`tests/test_outline.c` compares the outline reader with it); the application does not include it.
- `vendor/stb_image.h`: v2.30, public domain or MIT, Sean Barrett, copied unchanged from raylib's `src/external`; `vendor/stb_image.LICENSE.txt` records it.
  Used by `src/bmsrc.c` (PNG, GIF, BMP and TGA only).
- `src/outline.c` has its own inflate (structured after zlib's puff, written for this project); no zlib or sinfl is vendored.
- `testdata/outline` (the list is in `testdata/outline/README.txt`): subsets and conversions of Noto Sans (SIL OFL 1.1, `OFL.txt` beside them), a Unifont subset (GPLv2 or later with the font embedding exception, and OFL 1.1), and a Type 1 font written by `tests/gen_outline_fonts.py`.
- `testdata/bitmap` (`testdata/bitmap/README.txt`): tiny hand-made fonts made by `tests/gen_bitmap_fonts.py`, public domain (CC0 1.0); no real font file is copied.
