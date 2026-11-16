# pxfe handoff (2026-11-16)

Versions 5 to 20 are implemented: Load, Save, Import, Export (5), navigation (6), views (7), the design system with Noto Sans and GNU Unifont (8), font layers (9), importing a font file (10), the default glyph as a glyph of its own (11), locks instead of duplicates (12), importing a picture of a font (13) and moving the baseline and editing a glyph's facts (14) the Colours filter, typing with the mouse away and press-and-release (15) selection and moving in the glyph view (16), line, curve and pixel-perfect (17), the tool strip (18), rectangle and ellipse (19), and fill, eyedropper, polygon and contour (20).
The design is in [DESIGN.md](DESIGN.md), one section per version; "Version 5: Load, Save, Import, Export" replaces the storage tiers, the save pipeline and the drafts of version 4.
pxfe is meant to run on Windows, macOS and Linux; today it builds on Windows only (`build.bat`, `vendor/sqlite3/libsqlite3.a`, `src/winfile.c`).
The files of `handoff/` are the reports of the version 4 agents, kept for history.

## State

- `build.bat e2e` builds with no warnings.
- `build.bat test` passes: 138 pxfont, 44 selection, 110 raster, 248 outline, 143 bmsrc, 86 fit, 62 sheet, 120 fsettings, 101 pxtf and 172 store checks, 0 failed.
- `tests\e2e\run_all.bat` passes: 109 scripts, 0 failed (version 11 added 104 and 104b, version 12 added 105 and 105b, version 13 added 106, version 14 added 107 and 108, version 15 added 109, 110, 110b and 111, version 16 added 112 to 116, version 17 added 117 to 122, version 18 added 124, version 19 added 125 to 129, version 20 added 130 to 136).
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
- Typeface editing (the user's decision, changed in version 12): the typeface lock (`BROWSER_LOCK_TYPEFACE`, script name `@tf_lock`, in the Typeface tab and in the Typeface view's heading) opens the font of the selected variant, unlocks the typeface and shows the Typeface view again; `TypefaceEditing` (panel.inc) says that the open font's typeface is unlocked, and `DrawTypefaceTab` then edits `cont` (the old field code, without undo snapshots).
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
- The lock is one icon button (`Th_Lock` in `theme.h`) at every site: info bar, glyph bar, library cards, rows and selection blocks, the Typeface view (heading, font rows) and the Typeface tab; script names `@Unlock` and `@Lock`.
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

- `tests\e2e\run_all.bat` runs everything; a single script is started like the others, for example `tests\tmp\pxfe_e2e.exe tests\tmp\e2e.pxfdb --font 0820dc69a21b1cdb3a44d2cff7d884d8 --ini tests\tmp\e2e.ini --play tests\e2e\98_fl_locked.txt` after copying `testdata\lib.pxfdb` to `tests\tmp\e2e.pxfdb`.
- The scripts and their commands are in [tests/e2e/README.md](tests/e2e/README.md): 94 smoke, 95 sources and New Font, 96 adjustments and corrections, 97 to 97d the sidecar, 98 locked fonts, 99 edit and commit in place; asserts per script 17, 58, 57, 28, 25, 14, 24, 27 and 26.
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

## Version 10: importing a font file

The design is in the last section of [DESIGN.md](DESIGN.md), "Version 10: importing a font file".
Menu, Import Font File makes a new font (and typeface) from an outline or bitmap font file.
The file stays under the font as a font layer, and the glyphs are drawn from that layer until they are edited by hand.

- New files: `src/fit.c` and `.h` (the grid of a pixel font, the size of a font that is none, drawing a glyph as pixels; no raylib), `src/fitdoc.inc` (the follow engine, the Layers tab of the font view, the import dialog; included by `main.c`), `tests/test_fit.c`, `tests/fit_tool.c`, `tests/gen_pixel_fonts.py`.
- Changed: `src/outline.c` and `.h` (`outline_glyph_shape`, `outline_rasterize_glyph_xy`, `outline_name`; a simple TrueType glyph now stands `lsb - xMin` to the right, as fontTools draws it), `src/fsettings.c` and `.h` (schema version 2: `em`, `aspect`, `follow`, `mode`, `threshold`; a version 1 file is upgraded when opened), `src/flayers.c` and `.h` (absolute size, width, `FlLayer_FitParams`), `src/layers.c` (Size in px/em for an absolute size), `src/main.c` (the menu item, undo with the layer's settings, `FitTick`), `src/panel.inc` (the fourth tab, Layers), `src/play.inc`.
- `fit_find_grid` is a port of `pixelglyphlib/readers/ttf.py`.
  It gives the same grid as the Python for the 621 TrueType and OpenType files of `pixelglyphlib\sourcefiles` (3 of them with a pitch that differs in the third decimal).
  When nothing passes, the best lattice is offered as a guess.
- `fit_find_size` is new: the smallest size at which the letters and digits keep their parts and holes, then the best of the next two pixels.
- `tests\fit_tool.exe grid|size|show <font>` shows what the searches give for a file (the build line is in `tests/fit_tool.c`).

Things to know:

- A glyph follows the layer while it is exactly what the layer draws; nothing is stored for that.
  A glyph whose pixels were edited and edited back follows again.
- The follow layer needs an absolute size (`em > 0`); switching follow on for a version 9 layer turns its relative size into one.
- Undo restores the layer's settings with the font; a redraw is one undo step for all glyphs (steps within 0.8 s are one step).
- The searches and the redraw run on the UI thread: Noto Sans Regular (3094 glyphs) is searched in about a second and redrawn in a fraction of one.
- Not done: kerning, cap height and x-height of the file; a family of several files as one typeface; WOFF2.
- Size and offsets take any typed value (`Th_NumberF` in `theme.c`); "Optimize for This Size" keeps the size and searches position and threshold.
- A review by a second agent found ten points; fixed are: all corrections come back from the sidecar (the reader stopped at 512), an undo snapshot is only put back on the layer it was taken from, the placement of an original's followed layer is locked, a NaN or huge coordinate is refused, implied TrueType points no longer count as coordinates of the font, a negative advance becomes 0, a failed realloc in `sb_contour`, and the size search allows a small sample one glyph of slack.
  Open: a change of the followed layer's face or file makes every glyph count as edited by hand; serif faces get sizes near 19 px per em (Times New Roman), because their hairlines break below that.

## Version 11: the default glyph

The design is in [DESIGN.md](DESIGN.md), "Version 11: the default glyph".
The default glyph is a glyph of its own with the code `FFFFFF`, last in the font and in the grid, caption "Default".

- Bugs found in the old model (a pointer `_DEF` to another glyph, or a dangling one): it could not be edited or opened from the grid, deleting the glyph it pointed to dropped it, an uncoded glyph could not be the default, and a dangling default had no bitmap.
- `src/pxfont.c` and `.h`: no `defaultCodepoint`, `hasDanglingDef`, `defDx`, `defDy`, `defAdvance`, `defHash`; `px_font_default_index`, `px_font_set_default_from` (a copy of the selected glyph), the writer and parser treat `GLYPH FFFFFF` as a normal glyph, the uncoded count skips it.
- No schema change: the pxfdb already stored `FFFFFF` as a `pxfm_glyph` row.
- Tests: 138 pxfont checks (new ones for the glyph), `test_default_glyph` in `tests/test_store.c`, `104_default_glyph` (50 asserts, fails on the old executable) and `104b_default_reload`.

## Version 12: locks

The design is in [DESIGN.md](DESIGN.md), "Version 12: locks".
Every font and every typeface has a lock, locked by default; what is unlocked is edited in place, and everything that names it follows its new hash.
All of the duplicate machinery is gone (`Th_Duplicate*`, `BROWSER_DUPLICATE*`, `EDITOR_DUPLICATE`, `liveDups`, `CommitDuplicate`, `store_replace_commit`, `--duplicate`); the glyph-level Clone is a different feature and stays.

- `src/fsettings.c`: schema 3, table `unlocked(kind, hash)`; `fs_is_unlocked`, `fs_set_unlocked`, `fs_rename_font`, `fs_rename_typeface`; upgrade from schema 1 and 2.
- `src/store.c`: `store_edit_in_place` adds the new font, rewrites every typeface that names the old one, removes the old font and the glyphs no font names any more, and reports the typeface hashes that moved.
- `src/main.c`: `EditPending`, `CommitEdits`, `SetFontLocked`, `SetTypefaceLocked`, `LockedOf`, `ToggleLockOf`; `docStored` (false until a New Font or an imported font is committed); `readOnly` means "the font is locked", `contLocked` the typeface; `origHash` and `origContHash` are what the store holds and move at each commit.
- `src/browser.c`: `lock_button`, `Browser_SetLockQuery`, `Browser_Follow` (the Typeface view follows a moved typeface).
- A change of a lock counts as unsaved (the sidecar changes) while the font stays unmodified; the sidecar file exists as soon as something is unlocked.
- The old font can always be removed after the typefaces that name it are rewritten; an edit that leaves the hash as it was does nothing.
- Scripts: every script that edits starts with `click @Unlock`; `105_lock` and `105b_lock_reload` are the lock scenario (locked fonts refuse every edit tool, unlock, edit, the typeface and the layer follow the new hash, lock again, Save, reload keeps the edit and the lock state); 29, 31 and 32 were renamed `29_edit_replace`, `31_edit_twice` and `32_edit_back_to_original`, 98 and 99 `98_fl_locked` and `99_fl_edit_commit`.
- Not done: nothing removes the `unlocked` row of a font that is deleted; it is harmless.

## Version 13: importing a picture of a font

The design is in the last section of [DESIGN.md](DESIGN.md), "Version 13: importing a picture of a font".
Menu, Import Font File also takes a PNG, GIF, JPEG, BMP or TGA picture of a font and works out its background, scale, layout, characters and colours.

- New files: `src/sheet.c` and `.h` (the analysis; no raylib), `src/sheetref.c` and `.h` (the reference glyphs, drawn with `fit.h` from the fonts of `assets/fonts`), `tests/test_sheet.c`, `tests/sheet_tool.c`, `tests/gen_sheet_fonts.py`, `testdata/sheet` (CC0 pictures and two generated ones), `tests/e2e/106_import_sheet.txt`.
- Changed: `src/bmsrc.c` and `.h` (`bm_load_rgba`; JPEG decoding is switched on in stb_image), `src/fitdoc.inc` (the picture path of `ImpAnalyze`, `ImportSheetCreate`, `DrawImportSheetDialog`), `src/play.inc`, `src/main.c`, `build.bat` (test_sheet).
- `tests\sheet_tool.exe <picture> [-v]` prints what the analysis finds (the build line is in `tests/sheet_tool.c`).
- The collection the work was done on is `C:\Users\jnsGEzD3\Documents\GitHub\bitmapfont\color` (49 pictures, not in this repository): every layout is found, and 97.4 % of the characters whose truth is known are right.
  The truth and the scorer are not in the repository; they were scratch files of the session.

Things to know:

- A marker stands over a column between two glyphs; the column belongs to no glyph (the user's decision).
- The first picture of a run takes about two seconds: the reference glyphs are made then.
- A font of one ink colour is imported black on white; several colours give COLOR lines on a transparent background.
- Not done: the picture as a layer under the font; photographs brought down to a pixel grid; kerning.

## Version 14: the baseline, and the facts of a glyph

The design is in the last section of [DESIGN.md](DESIGN.md), "Version 14: the baseline, and the facts of a glyph".
In an unlocked font the baseline can be moved up and down (Font tab, FONT METRICS, the Baseline row), and the glyph view's Glyph tab edits the code point, character, advance and offsets.

- `src/panel.inc`: `BaselineWhy`, `CmdBaseline`, `BaselineRow`, `ParseHexCode`, `ParseCharCode`, `SetGlyphCode`; `GlyphFacts(px, y, gi, character, editable)` is shared by the glyph view (editable) and the font view's selection (text).
- `src/main.c`: `baseShift` and the arrays `undoBase`, `undoCode` (and the redo ones) beside the undo snapshots; `DoUndoRedo` moves the font layers by the difference and follows the glyph by its code.
- `src/fitdoc.inc`: `LayersShiftY`; `src/editor.c`: `Editor_SetGlyphIndex`; `src/gpanel.inc`: `DrawGlyphInfo(px, y, gi)`; `src/play.inc`: `assert eglyph`, `assert baseshift`, `#n` in `assert kern`.
- Tests: `107_baseline` (103 asserts) and `108_glyph_fields` (91 asserts), both in `run_all.bat`.

Things to know:

- The font layers are not in the undo snapshots; only the followed layer's settings are.
  The baseline's effect on all layers is undone through `baseShift`, so a layer added after a step also moves when that step is undone.
- A glyph without ink keeps its offset (0, 0) when the baseline moves, and its offset fields are dim.
- `Th_Button` acts on the release wherever the mouse is: the first edit of a font adds a hash box above the metrics and moves the rows under the mouse, so a press on Cap height + was released over Baseline down (script 02 found it).
  The Baseline buttons now act only when the press began on them (`BaselineRow`); the other release-triggered buttons keep the old behaviour.
- Not done: a keyboard shortcut for the baseline.

## Version 15: colours, typing, press-and-release

The design is in the last section of [DESIGN.md](DESIGN.md), "Version 15: filtering by colours, typing while the mouse is away, press-and-release".

- Colours filter: `StoreEntry.colors` and `LibInfo.colors` (`store.c` `FONT_SQL`, `library.c` `fill_info`), `selStr[2]` and `collect_colors` in `src/browser.c`, ini key `colors`.
- `src/theme.c` and `theme.h`: the two gates `Th_SetInputBlocked` (modal, popover) and `Th_SetMouseBlocked` (outside the visible area), with the classification in the comment of `theme.h`; `Th_Released` for the release-fired controls.
  The Baseline buttons' local workaround is gone.
- `src/play.inc`: `assert bcolors`, `bcolorvals`, `bcolorval<i>`, `tfocus`.
- Tests: `109_color_filter` (on `testdata/lib_colors.pxfdb`), `110_type_mouse_away`, `110b_type_mouse_away_glyph`, `111_press_release`.

Things to know:

- A font that states no ascent or descent has its info (and its colour count) loaded when the Colours list opens or a count is chosen.
- The count is of palette entries other than `.`, not of distinct RGBA values.

## Version 16: selection and moving

The design is in the last section of [DESIGN.md](DESIGN.md), "Version 16: selection and moving".
The glyph view has a rectangular marquee, a lasso, a magic wand and a Move tool, with select all, deselect, invert, delete, cut, copy, paste and the arrow-key nudge.
The behaviour is modelled on common pixel editors; the code is pxfe's own.

- New files: `src/selection.c` and `.h` (the cell mask, the wand, the polygon fill, the bitmap and float helpers; no raylib), `tests/test_selection.c` (in `build.bat test`).
- `src/editor.c`: the tool table `kTool[]` (the place where later tools join: a row with its group, a `Tool` value, an icon, a branch in `canvas_input`), the selection section (`float_lift`, `float_drop`, `float_cancel`, `sel_press`, `sel_drag`, `clip_*`), the marching ants (`draw_ants`), the bar's Set, Add, Sub, And control and the wand's 8-way toggle.
- `src/editor.h`: `Editor_Flush` (drops floating pixels; `main.c` calls it in `CommitEdits` and `CloseGlyphView`), `Editor_CancelFloat` (Ctrl+Z and Ctrl+Y of a float), the test hooks `Editor_Floating`, `Editor_SelCount`, `Editor_SelBounds`, `Editor_SelCell`, `Editor_ClipCount`.
- `src/theme.c` and `.h`: the icons `TH_IC_MARQUEE`, `TH_IC_LASSO`, `TH_IC_WAND`, `TH_IC_MOVE`.
- `src/play.inc`: `eselcount`, `efloating`, `eclip`, `assert esel ...`, and the key names `shift`, `ctrl`, `alt` for `keydown` and `keyup`.
- Since version 18 the tools are in a strip at the left of the canvas, and the bar has no narrow-window rules.

Shortcuts of the glyph view:

| Key | What it does |
| --- | --- |
| B, E, L, R, F | Pencil, Eraser, Line, Rectangle, Fill (as before) |
| M, Q, W, V | Marquee, Lasso, Magic Wand, Move |
| Shift at the press | add to the selection; Shift pressed after the press makes the marquee a square |
| Alt at the press | subtract from the selection (Shift and Alt together: intersect) |
| Ctrl at the press of a move | move a copy and leave the original |
| Ctrl+A, Ctrl+D, Ctrl+Shift+I | select all, deselect, invert |
| Arrow keys | with a selection: move its pixels by 1 cell; Shift+arrow by 8 cells |
| Shift+arrow | with no selection: move the whole bitmap by 1 cell (as before) |
| Enter | drop the floating pixels |
| Esc | cancel the float, then deselect, then close the glyph view |
| Delete, Backspace | clear the selected cells |
| Ctrl+C, Ctrl+X, Ctrl+V | copy, cut, paste (a float at the same place, or at the top left of the view) |
| Ctrl+Z, Ctrl+Y | while pixels float: throw them away; otherwise undo and redo as before |

Rules that were chosen:

- Pixels lifted by a move or a paste float; the glyph changes only at the drop, which is one undo step (a lift and a drop, a delete and a paste are one step each).
- A cell of the floating pixels that holds `.` is transparent: it does not overwrite what it lands on.
- The drop grows the bitmap as the pencil does and then crops it to its ink.
- The bar's MOVE buttons move the selection by 1 cell when there is one, the whole bitmap as before when there is not.
- A drag from inside the selection moves it only in Set mode without Shift or Alt; in the bar's Add, Sub and And modes it changes the selection.
- Alt+drag with a reference layer selected still moves that layer.
- The clipboard is the program's own (cells and pixel characters), kept for the session across glyphs and fonts.

Things to know:

- A locked font refuses Delete, Cut and Paste with the toast "The font is locked"; its selection tools are off.
- `assert esel bounds X Y W H` takes X as a column from the pen and Y as a row of the font box (0 is its top row), as `@ecell:x,y` does; `*` matches any X or Y.
- To hold a modifier across `mousedown`, `move` and `mouseup`, hold the key with `keydown shift` and release it with `keyup shift`; since version 17 `mousedown` and `mouseup` also take `right` and `middle`.
- Not done: undo of the selection itself, bar buttons for all, none and invert, the system clipboard, selection by colour range, rotating or scaling the floating pixels.

## Version 17: line, curve and pixel-perfect

The design is in the last section of [DESIGN.md](DESIGN.md), "Version 17: line, curve and pixel-perfect".
The Line tool gets Shift (constrained angle) and Ctrl (from the centre), the Pencil gets Shift+click lines and an optional pixel-perfect mode, a Curve (cubic Bezier, three drags) joins the Line button, and the drawing tools are clipped to the selection.
The behaviour is modelled on common pixel editors; the code is pxfe's own (textbook Bresenham and Bezier).

- New files: `src/raster.c` and `.h` (cell paths, no raylib), `tests/test_raster.c` (in `build.bat test`).
- `src/editor.c`: the `T_CURVE` row of `kTool[]` (`etool` 9; its own strip button since version 18), `put` clips to the selection (`clipped_out`), the stroke functions (`stroke_cell`, `stroke_to`, `stroke_end`: the pencil's pixel-perfect rule works on the cells of the stroke and takes a corner cell back), `shape_ends`, the curve section (`curve_press`, `curve_drag`, `curve_commit`, `curve_cancel`), the handles in `draw_canvas`, the second row's Pixel-perfect toggle.
- `src/editor.h`: `Editor_PixelPerfect`, `Editor_SetPixelPerfect`, `Editor_CurveStage`; `Editor_Flush` and `Editor_CancelFloat` also commit and cancel a curve being made.
- `src/main.c`: `pixelPerfect` is kept in `pxfe.ini` (`[app] pixelPerfect`).
- `src/play.inc`: `assert epp`, `assert ecurve`; `mousedown` and `mouseup` take `right` and `middle`.
- Tests: `117_line_shift_undo`, `118_pencil_shift_click`, `119_pixel_perfect`, `120_curve`, `121_clip_to_selection`, `122_line_curve_locked`; `123_shots_v17` (screenshots, not in `run_all.bat`).

New shortcuts of the glyph view:

| Key | What it does |
| --- | --- |
| L | Line (as before) |
| Shift+L | Curve (also its button in the strip) |
| Shift while dragging a line or the curve's end points | the angle snaps to 0, 14, 27, 45, 63, 76 or 90 degrees (the slopes 0, 1:4, 1:2, 1:1, 2:1, 4:1, 1:0) |
| Ctrl while dragging a line or the curve's end points | draw from the centre (the press cell is the middle) |
| Shift+click with the Pencil | a line from the last cell of the pencil or the line to the clicked cell |
| Right button | draws with `.`, for the pencil, the line, the curve, the rectangle and the fill (the complete table is in version 20) |
| Enter | draws the curve being made (after the first drag), as does another tool, leaving the glyph or the padlock |
| Esc | cancels the curve being made (before the selection is cleared and before the glyph view closes) |

Rules that were chosen:

- Ctrl, not Alt, is the centre modifier: Alt subtracts in the selection tools and moves a reference layer.
- Pixel-perfect is off by default and works on the pencil only (lines have no L corners and the curve always applies the rule); the toggle is shown for the Pencil, the Line and the Curve.
- With a selection, the Pencil, Eraser, Line, Curve and Rectangle draw only inside it; the Fill is clipped too since version 20.
  Pixels that float from an arrow key land when a drawing tool is pressed.
- Undo and redo clear the selection (as since version 16), so a script selects again after an undo.

Things to know:

- A curve being made is not in the undo stack and the glyph is unchanged until it is drawn; Ctrl+Z throws it away (`Editor_CancelFloat`).
- The padlock draws a curve that is half made (the commit before the lock does), as it drops floating pixels.
- Not done: brushes, editing the handles of a drawn curve, a curve of more than two control points.

## Version 18: the tool strip

The design is in the last section of [DESIGN.md](DESIGN.md), "Version 18: the tool strip".
The tool buttons moved from the glyph view's bar to a vertical strip at the left of the canvas: one column of 28 px buttons when they fit the height, else two.
The Curve has a button of its own, and the Line / Curve control of the second row is gone.

- `src/editor.c`: `kTool[]` groups are now 0 draw, 1 shapes, 2 fill, 3 selection; `draw_strip` lays the buttons out from the table (a row with `hidden` gets none) and returns the strip's width, the canvas rectangle starts after it; `draw_toolbar` lost the tool buttons, the `compact` rules and the Line / Curve control.
- `src/editor.h`: `Editor_ToolCols`; `src/theme.c` and `.h`: `TH_IC_CURVE`.
- `src/play.inc`: `assert etoolcols 1|2`; `resize` clears the window's minimum size first, so a script can test below 1110 x 600.
- Tests: `124_tool_strip`; `117_line_shift_undo` and `122_line_curve_locked` no longer use `@linemode.N`.

Things to know:

- The strip is two columns only when its height is short: at the smallest window (1110 x 600) ten tools still fit in one column.
- Later tools join by a row in `kTool[]` (name, key, icon, group): no layout code.
- Not done: tool options in the strip, a user-chosen strip side.

## Version 19: rectangle and ellipse

The design is in the last section of [DESIGN.md](DESIGN.md), "Version 19: rectangle and ellipse".
The Rectangle gets Shift (square), Ctrl (from the centre) and a Filled option, and the Ellipse joins the shape group of the strip.
The behaviour is modelled on common pixel editors; the code is pxfe's own (textbook midpoint ellipse).

- `src/raster.c` and `.h`: `ras_box` (the box of a drag with the two modifiers), `ras_rect`, `ras_ellipse` (Bresenham-type ellipse in a box; the pixel-perfect rule drops the L corners of the ring; filled = each row of the ring); `tests/test_raster.c` has 78 checks.
- `src/editor.c`: the `T_ELLIPSE` row of `kTool[]` (`etool` 10), `kStrip[]` (the order of the strip's buttons: Line, Curve, Rectangle, Ellipse; `kTool[]` keeps the numbers), `plot_shape` (preview and release of both tools; it replaced `plot_rect`), the second row's `Filled` toggle.
- `src/editor.h`: `Editor_Filled`, `Editor_SetFilled`, `Editor_StripTool` (test hook); `src/main.c`: `filledShapes` is kept in `pxfe.ini` (`[app] filled`); `src/theme.c` and `.h`: `TH_IC_ELLIPSE`.
- `src/play.inc`: `assert efilled`, `assert etoolslot<N>`.
- Tests: `125_rectangle`, `126_ellipse`, `127_shape_clip`, `128_shape_locked`, `129_strip_order`; `06_editor` and `121_clip_to_selection` use the Filled toggle where they used Shift.

New shortcuts of the glyph view:

| Key | What it does |
| --- | --- |
| R | Rectangle (as before) |
| O | Ellipse (E is the Eraser) |
| Shift while dragging a rectangle or ellipse | a square or circle: both sides take the longer one |
| Ctrl while dragging a rectangle or ellipse | draw from the centre (the press cell is the middle) |
| Esc while dragging | cancels the shape |
| Right button | draws with `.` (also for the ellipse) |

Rules that were chosen:

- Shift no longer fills the Rectangle (version 17 did); the bar's Filled toggle, shown for the Rectangle and the Ellipse, does, and it is kept in `pxfe.ini`.
- There are no Shift+R and Shift+O for a filled variant: one toggle covers both tools.
- The modifiers count while the button is down and at the release.
- The Ellipse ring is one cell thick, symmetric, touches its box on all four sides and has no L corner; a box one cell wide or high is a line, and two cells wide or high a solid band.
- Filled is the ring and its inside; the corner cells that the ring drops are not part of it either, so the outline of the filled shape is the ring.
- Both tools are clipped to the selection, grow the bitmap, and take one undo step; a locked font refuses them.

Things to know:

- Narrow ellipses (a short side of 3 or 4 cells and a long side twice that or more) end in a two-cell cap; a one-cell-thick ring is guaranteed from a short side of 5 up to 3 to 1.
- The preview builds the cell list again in every frame; a drag of thousands of cells is a list of that size.
- Not done: Shift+R and Shift+O, a rounded rectangle, a rotated ellipse, a thickness for the outline, editing a drawn shape.

## Version 20: fill, eyedropper, polygon and contour

The design is in the last section of [DESIGN.md](DESIGN.md), "Version 20: fill, eyedropper, polygon and contour".
It completes the tool families: the paint bucket gets Contiguous, 8-way and the clip to the selection, and the Eyedropper, the Polygon and the Contour are new.
The behaviour is modelled on common pixel editors; the code is pxfe's own (a textbook flood fill and an even-odd scanline fill).

- `src/raster.c` and `.h`: `ras_flood` (the cells of a paint-bucket fill: contiguous or global, 4 or 8 directions, an optional clip mask as a wall) and `ras_polygon` (the outline by the line raster, the inside by the even-odd rule); `tests/test_raster.c` has 110 checks.
- `src/editor.c`: the rows `T_PICK` (11), `T_POLY` (12) and `T_CONTOUR` (13) of `kTool[]` and their places in `kStrip[]`; `region_pix` (the glyph region's characters, shared by the wand and the Fill), `fill_at` (replaces `flood_fill`), `pick_at` and `alt_picks` (the eyedropper and Alt), the polygon section (`poly_press`, `poly_commit`, `poly_cancel`, `poly_back`), the contour (`cont_press`, `cont_drag`, `cont_cells`, drag mode `M_CONTOUR`), the previews in `draw_canvas`, the second row's Contiguous and 8-way toggles for the Fill and Filled for four tools, and `draw_strip` (a group that does not fit the rest of a column starts the next one).
- `src/editor.h`: `Editor_PolyVertices`, `Editor_Contiguous` (test hooks); `Editor_Flush` and `Editor_CancelFloat` also close and throw away a polygon being made.
- `src/theme.c` and `.h`: `TH_IC_PICK`, `TH_IC_POLY`, `TH_IC_CONTOUR`.
- `src/play.inc`: `assert epoly N`, `assert econtig 0|1`.
- Tests: `130_fill`, `131_fill_clip`, `132_eyedropper`, `133_polygon`, `134_contour`, `135_tools_locked`, `136_strip_v20`; `129_strip_order` has the new order, `124_tool_strip` and `129_strip_order` use a window of 1110 x 520 for the short case, `121_clip_to_selection` expects the clipped Fill.

The complete shortcuts of the glyph view:

| Key | What it does |
| --- | --- |
| B, E | Pencil, Eraser |
| L, Shift+L | Line, Curve |
| R, O | Rectangle, Ellipse |
| D, C | Polygon, Contour |
| F, I | Fill (paint bucket), Eyedropper |
| M, Q, W, V | Marquee, Lasso, Magic Wand, Move |
| 0 to 9 | pick that ink when the font's palette has it |
| Alt at the click | with the Pencil, Line, Curve, Rectangle, Ellipse, Polygon, Contour or Fill: pick the ink under the cursor, draw nothing, keep the tool |
| Right button | draws with `.` (pencil, line, curve, rectangle, ellipse, polygon, contour, fill) |
| Shift | Line and Curve ends and Polygon edges: clean angles; Rectangle and Ellipse: a square or circle; Pencil: a line from the last cell; with the selection tools: add |
| Ctrl | Line, Curve, Rectangle, Ellipse: draw from the centre |
| Alt (selection tools) | subtract from the selection; with Shift: intersect |
| Enter | draws the curve or the polygon being made; drops floating pixels |
| Esc | cancels a drag, the polygon or the curve being made, the float, the selection, then closes the glyph view |
| Backspace | the polygon being made: takes its last vertex back; otherwise clears the selection |
| Delete | clears the selection |
| Ctrl+A, Ctrl+D, Ctrl+Shift+I | select all, deselect, invert |
| Ctrl+C, Ctrl+X, Ctrl+V | copy, cut, paste |
| Ctrl+Z, Ctrl+Y | undo, redo; a float, a curve or a polygon being made is thrown away first |
| Arrow keys | move the selection by 1 cell, Shift+arrow by 8; with no selection Shift+arrow moves the bitmap |
| PgUp, PgDn | the previous and the next glyph |

Rules that were chosen:

- The Fill's region is the glyph box as the wand sees it (the advance across the font box, widened to the bitmap), so empty space is filled and the bitmap grows.
- The Fill is clipped to the selection: the selection is a wall and a limit, and a click outside it fills nothing (version 17 left it unclipped).
- Contiguous is on by default and is not kept in `pxfe.ini`; 8-way is the wand's switch, shared with the Fill.
- The Eyedropper has one ink to set: the right button picks as the left one does (pxfe has no second ink).
  Picking `.` sets the ink to `.`, which every font allows (the last swatch).
  A character that the palette does not hold is not taken.
- Alt at the press of a drawing tool is the temporary Eyedropper for that click, so the tool never changes; it does not apply to the Eraser or the selection tools.
  A selected reference layer other than the glyph itself wins: Alt+drag still moves it.
- The Polygon closes by Enter, by a click on the last vertex again (a double click) or on the first vertex; its corners are sharp (the vertices stay).
  The Contour's outline is pixel-perfect (the corner cells of its Ls go) and closes where the drag ended.
- Filled (one option) covers the Rectangle, the Ellipse, the Polygon and the Contour; the inside is the even-odd rule.
- A strip group that does not fit the rest of a column starts the next one: at 1110 x 600 the strip is two columns (the drawing, shape and fill groups, then the selection group), at 1110 x 480 three, in a tall window one.

Things to know:

- A polygon being made is not in the undo stack and the glyph is unchanged until it is closed; Ctrl+Z throws it away, and the padlock, another tool and leaving the glyph close it.
- Not done: vertex markers on the polygon being made, editing a drawn polygon, a Fill with a tolerance or a gradient, filling from a pattern, Contiguous kept in `pxfe.ini`.
