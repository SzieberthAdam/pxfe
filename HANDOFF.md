# pxfe handoff (2026-11-21)

> This file describes the current state only, by topic, without version history.
> It must stay under 25,000 characters (`wc -m HANDOFF.md`); when adding, shorten something else.

The design is in [DESIGN.md](DESIGN.md), under the same topic names as the sections below.
This file says where each topic lives in the code, how it is tested, and what to know before touching it.
pxfe is meant to run on Windows, macOS and Linux; today it builds on Windows only (`build.bat`, `vendor/sqlite3/libsqlite3.a`, `src/winfile.c`).
The files of `handoff/` are old agent reports, kept for history.

## State

- `build.bat` and `build.bat e2e` build with no warnings.
- `build.bat test` passes: 33 colormath, 274 pxfont, 44 selection, 110 raster, 249 outline, 148 bmsrc, 86 fit, 62 sheet, 121 fsettings, 137 pxtf, 230 store and 85 package checks (3 skipped), 0 failed.
- `tests\e2e\run_all.bat` runs 123 scripts (2026-11-21): `01_grid` and `07d_real` fail (their real font is not in the library).
  Timing flakes: each of `02_metrics`, `03_selection`, `04_delete`, `05_clone`, `06_editor`, `06b_palette`, `06c_noascent`, `23_modified_prompts`, `27_export_overwrite`, `42_layers_add`, `45_layers_discover`, `53_sample_persist`, `59_navigation`, `70_panel_tabs`, `101_import_font`, `139_many_colours` and `143_shots_picker` has failed in some full run and passed alone, more often while the machine is busy.
- `tests\e2e\run_reallib.bat` loads `pixelglyphlib\pxfdb.pxfdb` read-only and imports the folder into `tests\tmp\real.pxfdb`.
- Plain `build.bat` fails only at the link step when a running `pxfe.exe` locks the file; closing it is enough.

## Things to know

- The library and the specs now live in `C:\Users\jnsGEzD3\Documents\GitHub\pxfdb` (`spec/`, `pxtf/`, `pxfm/`, `pxgl/`, `palette/`, `license/`, no `.pxfdb` file); the `pixelglyphlib` folder is gone.
  `prep_real.ps1` reads `pxfdb` but its font `b86206e5…` is not there, so 01_grid and 07d_real fail with a warning; `run_reallib.bat` and the notes below on `pixelglyphlib` are out of date.
- The real library is never written by pxfe's tests.
  It holds two user-made fonts of 04b03 that its `pxfdb.pxfdb` lacks, so importing the folder gives more fonts than the file.
- The agents work only on copies under `tests/tmp/` and on `testdata/`.
- Every text file is LF; only the `.bat` files are CRLF on disk, because cmd needs it (`.gitattributes` enforces this; a `.bat` saved as LF breaks `call :label`).
- The scripts run fast: two clicks on one spot within 0.4 s are a double click, and a click right after a layout change needs `wait 6`.
- A single script is run like this, after copying `testdata\lib.pxfdb` to `tests\tmp\e2e.pxfdb`: `tests\tmp\pxfe_e2e.exe tests\tmp\e2e.pxfdb --font 3b52125b2e5f6466823baa4fa3186fa0 --ini tests\tmp\e2e.ini --play tests\e2e\98_fl_locked.txt`.
  The script commands are in [tests/e2e/README.md](tests/e2e/README.md).
- Every script that edits starts with `click @Unlock`; tools are pressed as `key b shift`, palette slots as `type 3` (slot keys come from `GetCharPressed`, in the e2e build `Play_GetChar`).
- Shot scripts (60, 62, 64 to 69, 100, 123, 142, 143) are not in `run_all.bat`; they write under `tests\tmp` and run a copy of the executable so a build is never blocked by an open window.
- `testdata` is made by `tests/make_testdata.exe`; `lib_colors.pxfdb` has real colour fonts, `lib_many.pxfdb` a font of 40 colours (`b6a511be…`).
- Script 28 has failed once in a full run and passed alone (a timing flake).

## Design system

- `src/theme.c` and `theme.h`: one component is one function, and a view never draws a second variant; the owner table is in DESIGN.md.
- Menus are all `Th_Menu`; popovers, dialogs and the toast use `Th_Raised`; `main.c` and `editor.c` both call `Th_StatusBar`; the lock is `Th_Lock` everywhere (`@Unlock`, `@Lock`).
- `Th_Stepper` is every zoom; `Th_FitText` takes `TH_F_TAIL`; `Th_HashShort` takes `TH_HASH_SMALL`; `Th_NumberF` takes any typed value.
- Fonts: Noto Sans v2.015 (`assets/fonts/noto-sans`, OFL 1.1) and GNU Unifont 18.0.01 (`assets/fonts/unifont`, OFL 1.1 or GPLv2+ with the font exception).
  Unifont glyphs are rasterised on demand into a 1024 x 1024 atlas that is flushed and refilled when full.
  The fonts are looked up beside the exe, then two folders up, then in the working folder; a missing file raises an error toast.
- Tests: `70_unifont`, `93_menu_keys`, the shot scripts 64 to 69.

## The font model

- `src/pxfont.c` and `.h`: the font, glyphs, the reader and writer of the pixelglyphlib formats (pxfm version 3: the spec working tree after commit 5dd97ca2c4), drafts (`px_save_draft`, `px_load_draft`: unused by the application, still tested by `test_pxtf.c`).
- Codes are `PxCode` (`int64_t`, the number `GCCCCCCVVV` of the spec): `px_code_cp`, `px_code_symbol`, `px_code_variant`, `px_code_text`, `px_code_parse`, `px_code_label`, `px_code_parse_user`, `px_font_find_code`, `px_font_find_cp`, `px_font_free_variant`.
  Version 2 texts, version 2 and 3 drafts and a pxfdb with version 2 codes are refused with a message.
- The default glyph is a glyph of its own with code `I+DEFA00-000` (`PX_CODE_DEFAULT`): `px_font_default_index`, `px_font_set_default_from`.
- `SYMBOLIC`: the Symbolic switch in `src/panel.inc`, the tag in `src/browser.c`, `outline_symbolic` and `BmStrikeInfo.symbolic` set it on import.
- Tests: `test_pxfont.c`, `104_default_glyph`, `104b_default_reload`, `138_symbolic`.

## Typefaces (pxtf version 2)

- `src/pxtf.c` and `.h` read and write pxtf version 2 (pxfdb spec `pxtf.md`): the `NAME` top line (`PxtfTypeface.name`, `pxtf_set_name`), and in every variant `PXFM`, `SOURCE-FORMAT`, `SOURCE-HASH` and `SOURCE-TYPE` (`format`, `source`, `type`); version 1 top lines and the key `FAMILY` are refused.
- Code names follow the keys: `PXTF_NAME` (the variant's `NAME`), `LibEntry`/`StoreEntry` `typefaceName` and `fontName`, `Browser.tfName`, `fimp.typefaceName`; the import dialogs label the name "Typeface".
- A font made in pxfe: `pxtf_set_editor_source` (`SOURCE-FORMAT PXFE`, `SOURCE-TYPE pxfe`, a random `SOURCE-HASH` from `pxtf_random_hash`).
- The UI shows no source format (cards, box headers, the Typeface view, META).
- `tests/make_testdata.c` writes the version 2 fixtures; Test Sans is `236dc4b7…`, Uncoded Mono `0021a99d…`.
- `test_package.c` skips its byte-identity check when the Python reference refuses the `NAME` top line.
- Tests: `test_pxtf.c`, `test_store.c`, `12_typeface` (`assert tfname`), `146_new_typeface`.

## Licence terms

- pxtf "Licence terms" (`TERMS-*`) in `src/pxtf.c`: the text keys are `PxtfText` (`PXTF_TERMS_*`); the word, date and hash keys stay raw in `opts` (`pxtf_opt`, `pxtf_opt_all`); `pxtf_terms_words` and `pxtf_terms_meaning` hold the words with their plain meanings; `check_terms` and `terms_ok` (the cross rules) run in the parser; `pxtf_license_short`; `pxtf_license_canonical` (license.md).
- `src/store.c`: the `license` table (zlib level 9 when shorter, `z_compress9` and `z_uncompress` since vendor/zlib has no compress.c), `store_license_text`, `license/xx/<hash>.txt` in Import, Export and the scan; a `TERMS-TEXT` without its text stops an Import.
  `StoreTerms` (`store_terms_of_variant`, `list_terms` from the field rows) is in every `StoreEntry` and `LibEntry`.
- `src/package.c`: a pxtp carries `license/<hash>.txt` for every `TERMS-TEXT`; a pxfp has none.
- `src/browser.c`: the pill (`licence_chip`, `licence_press`, `Browser_SetLicenceOpen`), the LICENCE section of the View area (`licence_filter_ui`, `licMask`, `licNoOblig`, `licence_match`); `src/panel.inc`: the licence dialog (`li`, `LicenceOpen`, `LicTable`, `DrawLicenceDialog`).
- Test data: Test Sans 0001 is CC-BY-4.0 with the licence text `267033f4…`, 0002 free for personal use and derived, Uncoded Mono has no terms.
- Tests: `test_pxtf.c` (`test_terms`), `test_store.c`, `test_package.c`, `147_licence`.

## Palettes and colours

- `src/pxfont.c`: hex palettes (`px_hex_parse`, `px_hex_text`); the palette table `PxFont.pals` (entry 0 the default black palette), `PxGlyph.pi`, `PxPalette.draw`; copy on write (`px_font_intern_palette`, `px_glyph_add_color`, `px_glyph_set_palette`, `px_glyph_set_colors`); canonical order (`px_glyph_canonicalize`, `px_glyph_used_colors`, `px_glyph_add_slot`, `px_glyph_replace_color`, `px_glyph_trim_palette`); the font's colour list (`px_font_color_list`); `px_font_compact_palettes`.
- `src/colormath.c` (no raylib): sRGB gamma, HSV, HSL, CIE Lab, hex, luminance.
- A colour font is read whole when it is opened; fonts of one colour stay lazy.
- Undo snapshots compact the palette table; the glyph view canonicalizes the open glyph after every frame without a drag and then compacts, so `assert palettes` counts no leftover entry.
- The font's colour list is in order of first appearance, so it can reorder while drawing; the ink stays.
- Tests: `test_colormath.c`, `test_canonical_palettes`, `137_colour_font`, `137b_colour_font_reload`, `139_many_colours`.

## Storage: Load, Save, Import, Export

- `src/store.c`: an in-memory SQLite database that Load fills from a `.pxfdb`; Save writes a temp file with `VACUUM INTO` and replaces the file; a change counter tells whether memory differs from the file; Import and Export work on folders; the `palette` table, `pxfm.symbolic`, `pxfm.colors`, `store_palette_text`.
- The pxfdb is format version 2 (`pxtf.name`, `pxtf_variant.source`); a version 1 database is refused ("import the library folder again").
- A pxfdb made before hex palettes is refused on open ("made by an older pixelglyphlib … rebuild it"); in `pixelglyphlib`, `pxfdb.pxfdb` opens and the two `pxfdb-with*-symbols.pxfdb` are refused.
- New Database (`store_create`, `NewDatabase` in main.c): an empty store with no file (path ""), titled "Untitled"; `SaveDb` turns into Save As for it; script 07f.
- `src/main.c`: the menu items, the Save button, the title and status bar markers, the Save / Discard / Cancel prompts, the export dialog (Update, Overwrite, backup).
- `src/fsettings.c`: the sidecar `<db>-settings` (SQLite, schema 4: `layer`, `correction` by `PxCode`, `unlocked`; schema 1 to 3 are upgraded on open).
  Save writes it (alone when only settings changed); Save As writes a new one; Import starts empty; Export does not touch it; with nothing to keep it is not written and an old one is deleted.
- Save runs on the UI thread; on the real library it freezes the window for the length of `TRACE saveas`.
- SQLite gets Windows ANSI paths; a path with characters outside the code page is not supported.
- Tests: `test_store.c`, `test_fsettings.c`, `test_pxtf.c`, scripts 15 to 18b, 23 to 31, 52b, 07a, 07b.
- `build.bat` links zlib (`%ZLIB%`) wherever it links the store.
- Font and typeface packages (`.pxfp`, `.pxtp`): `src/package.c` and `.h` (API in the header, DESIGN.md "Packages (pxfp, pxtp)"); zlib 1.4 vendored in `vendor/zlib`, linked by `build.bat`; `store_add_files` at the end of `store.c`.
  Export Font… and Export Typeface… of the Font and Typeface tabs write one (`ExportPackage`, panel.inc; `setdialog save` answers its dialog).
  Not in the UI yet: Import Package (`pkg_import`, then rescan; a store change, saved by Save).
- Open: package bytes depend on the deflate implementation (zlib, as pxfe uses, against zlib-ng in Windows Python); importing is not affected.
  Tests: `test_package.c` with `tests/pkg_reference.py`, script 70.

## Locks

- Every font and typeface has a lock, locked by default; unlocked means edited in place.
- `src/store.c`: `store_edit_in_place` adds the new font, rewrites the typefaces that name the old one, removes the old font and orphan glyphs, and reports the moved typeface hashes.
- `src/fsettings.c`: `fs_is_unlocked`, `fs_set_unlocked`, `fs_rename_font`, `fs_rename_typeface`.
- `src/main.c`: `EditPending`, `CommitEdits`, `SetFontLocked`, `SetTypefaceLocked`, `LockedOf`, `ToggleLockOf`, `docStored`; `readOnly` means the font is locked, `contLocked` the typeface; `origHash` and `origContHash` move at each commit.
- The padlocks are only in the Typeface and Font tabs (`DrawInfoTab`, panel.inc) and in the glyph view's bar.
  Cards, rows and headings show an open padlock as a mark when unlocked (`unlock_mark`, browser.c; `Browser_SetLockQuery`).
- Unlocking a library font or typeface opens its font quietly and stays in the view (`ToggleLockOf`).
- The typeface lock governs the typeface-wide fields; the font lock governs the font's own fields (style, full name, weight name, slant, italic, underlined, struck-out), which live in its typeface variant.
- A change of a lock counts as unsaved (the sidecar changes) while the font stays unmodified.
- The `unlocked` row of a deleted font is not removed (harmless).
- Tests: `105_lock`, `105b_lock_reload`, `29_edit_replace`, `31_edit_twice`, `32_edit_back_to_original`.

## Navigation and views

- `src/main.c`: `NavView`, `DrawNav`, `NavBack` (Alt+Left), `ShowTypefaces`, `ShowFonts`, `ShowTypeface`, `ShowGlyphs`, `ShowFont`, `CloseGlyphView`, `HandleInsightTarget`.
- `src/play.inc`: `nav` (0 Typefaces, 1 Typeface, 2 Fonts, 3 Font, 4 Glyph, 5 Glyphs), `navactive`, `intypeface`, `assert navseg`; the segments are `@nav.0` to `@nav.2`.
- `src/finder.c`: the Glyphs view, its side tabs `fside` (View, Usage), `Finder_SelectedGlyph`.
- Tests: `59_navigation`, `59b_navigation_orphan`, `61_views`.

## Library browser

- `src/browser.c`: the Typefaces, Typeface and Fonts views; the View area (`view_area`, ini key `viewopen`) and the side tabs Typeface | Font | Similar (Font | Similar in the Fonts view; `SIDE_*`, ini key `sidetab`; `SIDE_VIEW` is read as Font), `sel_typeface`; the Typeface view (`tview_ui`, `Browser_OpenTypeface`, `Browser_CloseTypeface`); the Colours switch (`colorOnly`, ini key `colouronly`; `StoreEntry.colors`, `LibInfo.colors`); the Columns stepper reads `autoCols` from `grid_ui`.
- A press on the selected entry opens it; there is no context menu, no pinned selection block and no button on cards or rows.
- The Typeface and Font tabs are drawn by main.c through `Browser_SetInfoTab` (`DrawLibraryTab`, `DrawInfoTab`).
  Their commands (lock, Clone Font, Export, Delete) are queued with `LibCmd` and run at the end of the frame (`RunLibCommand`).
- Clone Font (`CloneFont`, panel.inc; `pxtf_clone_variant`) adds a variant naming the same font, style "<style> copy"; the typeface must be unlocked.
- New Typeface (the "+" after the boxes, `BROWSER_NEW_TYPEFACE`, `NewTypeface`, `store_new_typeface`) makes a typeface with no variant named "New Typeface" (then "New Typeface 2"…: an empty typeface of a taken name is refused); it is listed as one entry of variant 0 and pxfm "" (`list_empty_typefaces`), drawn as a header-only box.
- Add Font… (ACTIONS of the Typeface tab, `LC_ADDFONT`, the `af` dialog, `AddFontToTypeface`) adds a font of the library as a new variant with the source lines and reading options of its old variant (the editor's when it has none); the font stays in its other typefaces.
- META's Name field (`@c.typeface`) edits the open document's typeface, or renames another unlocked one at once (`LC_RENAME`, `RenameTypeface`).
  The two variants share the font until one is edited: `CommitEdits` points only the open variant at the new font and `store_edit_in_place` keeps the old font for the other.
- The Colours count is of palette entries other than `.`; a font that states no ascent or descent has its info loaded when the list opens or a count is chosen.
- While a field has the keyboard the browser reads no keys (`extTyping`).
- Tests: `109_color_filter`, `12_typeface`, `70_panel_tabs`.

## Font view

- `src/panel.inc`: the View area (`DrawFontView`: the zoom, Metrics and Captions; `fontView`, ini key `viewFont`), then the tabs Font | Glyph | Similar | Layers (`panelTab` 0, 1, 2, 3; ini key `fontTab`, an old 4 is read as 0); Font has `DrawInfoTab` and the metrics; Glyph has the selection (with its character), the glyph commands (`ButtonRow`), the glyph hash and the usage.
- There is no command bar, no info bar, no "..." menu and no grid context menu; the toolbar keeps Menu, the navigation, Undo, Redo and Save.
- The Baseline row (`CmdBaseline`, `BaselineRow`); `GlyphFacts` (editable in the glyph view, text in the font view); `ParseHexCode`, `ParseCharCode`, `SetGlyphCode`.
- `src/main.c`: `baseShift`, `undoBase`, `undoCode`; `DoUndoRedo` moves the font layers by the difference and follows the glyph by its code.
- The font layers are not in the undo snapshots; the baseline's effect on them is undone through `baseShift`, so a layer added after a step also moves when that step is undone.
- New Font (Menu, first item) makes an empty orphan font; Add Glyph adds a glyph.
- Tests: `107_baseline`, `108_glyph_fields`.

## Glyph view

- `src/editor.c` and `.h`: the canvas, the bar, the status line; `Editor_SetGlyphIndex`; `Editor_ViewPanel` (the glyph field `eglyph` with its arrows, zoom, Fit, Metrics, Checker) is the View area of `gpanel.inc` (`glyphView`, ini key `viewGlyph`), above Glyph | Layers | Kerning (`glyphTab` 0, 2, 4; the glyph usage is in the Glyph tab).
- The bar is one row of `BAR_H` (44, the panel's `PANEL_TABS_H`); the View area's neighbours and the glyph field reach the editor through `navReq` (`EDITOR_GOTO` with `Editor_GotoIndex`), returned by the next frame.
- The sample strip (`SampleStrip_Draw`) is under the glyph view as under the grid: it folds, resizes and has its own zoom.
- `Editor_Flush` (called by `CommitEdits` and `CloseGlyphView`) drops floating pixels and draws or closes a curve or polygon being made; `Editor_CancelFloat` throws them away (Ctrl+Z).
- `src/main.c` and `src/sample.c` draw with each glyph's own palette.
- Tests: `06_editor`, `110b_type_mouse_away_glyph`.

## Kerning

- `src/editor.c`: `kern_partner`, `kern_value`, `kern_pen`, `set_kern`, `kern_set_partner`, `kern_neighbour`, `kern_toggle`, `kern_label`, `kern_parse`, `kern_hit`; the drag mode `M_KERN`; `Editor_KernPanel` is the Kerning tab.
- `src/editor.h`: `Editor_SetKerning` (main.c: on while `glyphTab` is 4), `Editor_TakeKernRequest` (Shift+K), `Editor_Kerning`, `Editor_KernPartner`, `Editor_SetKernPartner` (nothing calls the last one yet).
- The editor keeps its own keys off while any text field has the keyboard.
- `src/pxfont.c`: `px_font_set_kern` (0 removes the pair), `px_font_kern`.
- Tests: `144_kerning` (`assert ekern`, `assert ekernpartner`, `assert kern`).

## Tools of the glyph view

- `src/raster.c` and `.h` (no raylib): lines, curves, `ras_box`, `ras_rect`, `ras_ellipse`, `ras_flood`, `ras_polygon`.
- `src/editor.c`: the tool table `kTool[]` (groups 0 draw, 1 shapes, 2 fill, 3 selection; a new tool is a row there plus a branch in `canvas_input`, no layout code), `kStrip[]` (the strip's order), `draw_strip`; `put` clips to the selection; `stroke_cell`, `stroke_to`, `stroke_end`, `shape_ends`, `plot_shape`, `curve_*`, `poly_*`, `cont_*`, `fill_at`, `region_pix`, `pick_at`, `alt_picks`.
- `src/editor.h`: `Editor_ToolCols`, `Editor_PixelPerfect`, `Editor_Filled`, `Editor_CurveStage`, `Editor_PolyVertices`, `Editor_Contiguous`, `Editor_StripTool`.
- `pxfe.ini [app]` keeps `pixelPerfect` and `filled`; Contiguous is not kept.
- Curves and polygons being made are not in the undo stack; undo and redo clear the selection.
- `src/play.inc`: `resize` clears the window's minimum size first, so a script can test below 1110 x 600; `mousedown` and `mouseup` take `right` and `middle`.
- Tests: `test_raster.c`, scripts 117 to 122, 124 to 136.

## Selection and moving

- `src/selection.c` and `.h` (no raylib): the cell mask, the wand, the polygon fill.
- `src/editor.c`: `float_lift`, `float_drop`, `float_cancel`, `sel_press`, `sel_drag`, `clip_*`, `draw_ants`; test hooks `Editor_Floating`, `Editor_SelCount`, `Editor_SelBounds`, `Editor_SelCell`, `Editor_ClipCount`.
- In scripts, `assert esel bounds X Y W H` counts X from the pen and Y from the font box's top row (`*` matches any); a modifier is held with `keydown shift` and `keyup shift`.
- Tests: `test_selection.c`, scripts 112 to 116.

## The palette bar and the colour picker

- `src/editor.c`: the palette bar under the canvas (row 1 `.`, the slots and `+`; the font's other colours below), drag and drop, double-click edit, the reorder animation, slot keys, the unused-slot rule (`leave_glyph`, `remember_p0`); `Editor_BarRows`, `Editor_BarExtras`, `Editor_Ink` (the ink's slot), `Editor_InkColor`.
- `src/colorpick.c` and `.h`: `Th_ColorPickOpen`, `Th_ColorPickTake`, `Th_ColorPickIsOpen`, `Th_ColorPickDraw`, `Th_ColorPickMode`, `Th_ColorPickValue`.
- `src/main.c` draws the picker last and blocks the rest under it (`HandleShortcuts` returns early), keeps `pickerMode` in `[app]`; the picker's first frame ignores clicks and keys.
- `src/play.inc`: `ebarrows`, `ebarextras`, `eunused`, `eslotof:RRGGBB`, `eslotused:N`, `epicker`, `epickermode`, `epickerrgb`, `epickera`, `epal`, `ecolors`, `einkrgb`.
- Tests: `06b_palette`, `132_eyedropper`, `140_palette_keys`, `141_palette_drag`; `08a_quit_prompt` and `44_layers_pixels` keep a plain `key l` on purpose (it must change nothing).

## Font layers

- `src/flayers.c` (source, state, glyph cache, corrections, `FlDoc`), `src/layers.c` (the Layers tab, the Add Layer popover), `src/outline.c` (TrueType, CFF, WOFF, EOT, Type 1 PFB, TTC; its own inflate, after zlib's puff; no WOFF2), `src/bmsrc.c` and `src/bmsrc_cp.inc` (BDF, PCF, FNT, FON, image sheets with a JSON descriptor, bitmap strikes), `src/fitdoc.inc` (`LayersShiftY`).
- A locked font may get layers and its `.pxfdb` stays byte for byte as it was (script 98).
- Tests: `test_outline.c` (compares with `vendor/stb_truetype.h` v1.26, tests only), `test_bmsrc.c`, scripts 94 to 99.
- Licences: `vendor/stb_image.h` v2.30 (public domain or MIT, `vendor/stb_image.LICENSE.txt`); `testdata/outline` (Noto Sans subsets under OFL 1.1, a Unifont subset, a Type 1 font from `tests/gen_outline_fonts.py`; list in its `README.txt`); `testdata/bitmap` (tiny fonts from `tests/gen_bitmap_fonts.py`, CC0 1.0).

## Importing a font file

- `src/fit.c` (no raylib): `fit_find_grid` (a port of `pixelglyphlib/readers/ttf.py`, agreeing with it on the 621 files of `pixelglyphlib\sourcefiles`), `fit_find_size`, drawing a glyph as pixels.
- `src/fitdoc.inc`: the follow engine, the Layers tab of the font view, the import dialog, `FitTick`.
- A glyph follows the layer while it is exactly what the layer draws; nothing is stored for that.
- The searches and the redraw run on the UI thread (Noto Sans Regular: about a second); a redraw is one undo step.
- A change of the followed layer's face or file makes every glyph count as edited by hand; serif faces get sizes near 19 px per em.
- Tools: `tests\fit_tool.exe grid|size|show <font>` (build line in `tests/fit_tool.c`), `tests/gen_pixel_fonts.py`.
- Tests: `test_fit.c`.

## Importing a picture of a font

- `src/sheet.c` (the analysis; no raylib), `src/sheetref.c` (reference glyphs drawn from `assets/fonts`), `src/bmsrc.c` (`bm_load_rgba`, JPEG on), `src/fitdoc.inc` (`ImportSheetCreate`, `DrawImportSheetDialog`).
- The first picture of a run takes about two seconds (the reference glyphs are made then).
- On `C:\Users\jnsGEzD3\Documents\GitHub\bitmapfont\color` (49 pictures, not in this repository) every layout is found and 97.4 % of the known characters are right; the truth and the scorer were scratch files.
- Tools: `tests\sheet_tool.exe <picture> [-v]`, `tests/gen_sheet_fonts.py`; `testdata/sheet` (CC0 pictures and two generated ones).
- Tests: `test_sheet.c`, `106_import_sheet`.

## Input rules

- `src/theme.c` and `.h`: `Th_SetInputBlocked` (modal, popover) and `Th_SetMouseBlocked` (outside the visible area), classified in the comment of `theme.h`; `Th_Released` for release-fired controls, which act only when the press began on them; `Th_TextIsIn`.
- While a menu is open its keys reach nothing else: `IsKeyPressed` and `IsKeyPressedRepeat` are wrapped in `theme.h` (`Th_MenuKeys`), and `Th_Menu` calls the raw functions; `Th_MenuHi` and `assert menuhi N` expose the highlight.
- Tests: `110_type_mouse_away`, `111_press_release`, `93_menu_keys`.
