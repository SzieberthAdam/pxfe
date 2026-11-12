# End-to-end tests

The scripts in this folder drive a console build of pxfe through the raylib automation player (`src/play.inc`).

## Running

`tests\e2e\run_all.bat` builds `tests\tmp\pxfe_e2e.exe` (`build.bat e2e`), prepares the real library font (`prep_real.ps1`) and runs every script.

Before each script that mutates something, `testdata\lib` is copied to `tests\tmp\e2elib` (the source of the import tests, and a snapshot that must stay untouched) and `testdata\lib.pxfdb` to `tests\tmp\e2e.pxfdb` (the database the scripts load).
Export folders are `tests\tmp\xout`, backups `tests\tmp\xout_backup.pxfdb`.

The settings file (`--ini`) is in `tests\tmp`.

Nothing is ever written into `pixelglyphlib`.

The exit code is 0 when everything passed, 1 when an assertion failed, 2 when the setup failed.

The e2e build is compiled with `-DGetCharPressed=Play_GetChar`, so typed text reaches every module (the browser, the glyph finder), not only `main.c`.

The player keeps running when the window is minimised (`FLAG_WINDOW_ALWAYS_RUN`).

The scripts run at 240 frames per second, so two clicks on one spot within 0.4 s of real time are a double click (use `sleep 600`), and a click that follows a layout change needs a `wait 6` first (a widget is found by where the last frame drew it).

## Model under test

pxfe works on one `.pxfdb` database: Load reads it into memory (an in-memory SQLite store) on a worker thread, with a progress screen.

Memory is changed by committing duplicates and by deleting fonts and typefaces; the file is changed only by Save and Save As (a temp file in the same folder, then a replace).
The modified state is a change counter, shown in the window title ("name.pxfdb — modified"), the status bar and the Save button.

An original (opened from the database) is read-only.
"Duplicate to Edit" makes an editable deep copy.
The modified marker of a font depends only on its hash (and that of the typeface).
A duplicate is committed into memory when its font view is left, and before Save, Export, Load, Import and quit.
A later commit of the same duplicate replaces the earlier one.

Import Folder reads a folder of pxtf, pxfm and pxgl files with strict readers into a new `.pxfdb` and loads it.
Export to Folder writes memory into a folder: Append adds what is missing, Overwrite deletes the contents of `pxtf/`, `pxfm/` and `pxgl/` first, with an optional backup (the folder's current data imported into a `.pxfdb`).

Glyphs without a code point show as `#n` and are renumbered when one is deleted.

## Script language

One command per line, `#` starts a comment.

- `wait N` waits N frames, `sleep MS` waits wall-clock time.
- Two clicks on one spot closer than 0.4 s are a double click: after `dblclick` that opens a view, `sleep 600` before clicking there again.
- `click`, `dblclick`, `drag`, `move`, `wheel`, `mousedown`, `mouseup`, `key`, `type`, `closewindow` send input.
- Targets: `@Name` (a widget registered by the frame, spaces become `_`), `@cell:U+0041`, `@cell:#0` (an uncoded glyph), `@cellorigin:U+0041`, `@ecell:x,y` (y is a row of the font box, 0 is its top), `@card:N`, `@fcell:N` (a result of the glyph finder), or two numbers as screen coordinates.
- `assert <name> [op] <value>` checks state: `glyphs`, `selected`, `modified` (the font differs from its original), `unsaved` (memory differs from the file, or a duplicate has edits that are not in memory), `dbmodified` (the change counter), `duppending`, `readonly`, `hascont`, `m.<metric>`, `has.<metric>`, `ratioa`, `ratiob`, `mono`, `default`, `kerncount`, `undo`, `redo`, `editor`, `inbrowser`, `btotal`, `bvisible`, `c.italic`, and more.
- Database state: `hasstore`, `storeimported` (1 when it was just imported), `storefonts`, `storeglyphs`, `storetypefaces`, `loading`, `opentime` (ms), `infont`, `infinder`, `uncoded`.
- Insight and finder state:
  `usage` (fonts that hold the selected glyphs), `usagevar` (typeface variants that name them), `usagecode` (fonts that hold the single selected glyph under its code, -1 when none), `similar` (entries of the similar list), `insightbusy`.
  `finder_count` (bitmaps found for the code), `finder_code`, `fcellfonts` (the "N fonts" of the first finder cell).
- Panel tabs: `ptab` (right panel of the font view: 0 Font, 1 Typeface, 2 Glyph, 3 Similar), `bside` (side panel of the browser: 0 View, 1 Similar).
  The tab buttons are `@ptab_Font`, `@ptab_Typeface`, `@ptab_Glyph`, `@ptab_Similar`, `@bside_View`, `@bside_Similar`.
  The metric and typeface widgets (`@ascent.plus`, `@c.family`…) exist only on their tab, so a script clicks the tab first.
  The rows of the usage and similar lists are `@usage_row_N` and `@similar_row_N`, and the panel's link is `@glyph_find`.
- `assert glyph <cp> exists|size|top|y|x|advance|ink|pixel|hash`, where `<cp>` is `U+0041` or `#n`.
- `assert caption <glyph> <text>` checks the code label of the grid (`U+0041`, `#0`).
- `assert sql "<query>" <expected>` runs a query on the open store (first column of the first row), `assert sqlfile <file> "<query>" <expected>` on a `.pxfdb` file.
- `assert ctext`, `assert kern`, `assert title`, `assert wtitle <text>` (the window title), `assert statusdb <text>` (file, modified or saved in the status bar), `assert modal`, `assert toast`, `assert hash`, `assert saved pxfm|pxtf`, `assert file <path> exists|contains|notcontains|lines`, `assert samefile`, `assert dirfiles <dir> <op> <N>`, `assert dirhas <dir> <text>`, `assert snapdir unchanged|added N`, `assert pixel`.
- Browser state: `btab`, `bzoom`, `bcols`, `bsel` (visible index of the selection), `bpending`, `assert bfilter <text>`, `assert bsample <text>`, `assert bselpxfm <hash>`.
- Typefaces tab (one box per typeface):
  `bvisible` counts variants, `btypefaces` the typefaces of the library (the badge of the tab), `bboxes` counts boxes, `bboxvars<N>` the variants shown in box N, `bselhdr` is 1 while a box header (the typeface) is selected, not a variant row.
  `@card:N` is the N-th visible variant row (in the Fonts tab the N-th card), `@hdr:N` the header of box N.
  `bink<N>` counts the lines of the sample of visible entry N that show ink (-1 until laid out).
  `blast` is the BrowserAction of the last choice the browser returned (1 open, 2 duplicate, 3 insight, 4 delete font, 5 delete typeface), `assert blastpxfm <hash>` and `assert blastpxtf <hash>` check its hashes (a prefix is enough).
  Right click: `click @card:0 right`; the side panel's buttons are `@Delete_Font` and `@Delete_Typeface` (`assert w.Delete_Font 0|1`); the context menu items are `@Ctx_Open`, `@Ctx_Duplicate_to_Edit`, `@Ctx_Delete_Font`, `@Ctx_Delete_Typeface`.
- Sample (shared by the browser and the font view): `assert sample <text>` (`\n` is a new line, `-` is empty), `samplelines`, `samplefocus`, `setclip <text>` puts text on the clipboard for Ctrl+V.
  The browser's box is `@Sample`, the strip's `@Sample_Edit`.
  Strip state: `stripzoom`, `striph` (stored height), `stripcollapsed`; widgets `@sz_1` to `@sz_8`, `@Sample_Resize` (the top edge), `@Sample_Collapse`, `@Sample_Preview`, and `@Sample_Origin`, the top left of the first line's font box (for `assert pixel`).
- `assert hashname` checks that the current hash of the font (and of its typeface) equals the hash it was opened with, names a row of the store and, for a root, a file of the library.
- `waitfor <name> <op> <value> [ms]` idles until a state of `assert` holds (default timeout 60 s, a timeout is a failure).
- `snapdir <dir>` takes the snapshot used by `assert snapdir`.
- Glyph view (version 4): `gtab` (right panel: 0 Glyph, 1 Usage, 2 Layers; the tab buttons are `@gtab_Glyph`, `@gtab_Usage`, `@gtab_Layers`), `emetrics` (metric lines on), `ehidden` (the edited glyph's layer is hidden), `ezoom`.
  The bar has `@ezoom-`, `@ezoom+`, `@ezoom.fit`, `@Metrics` and, in an original, the chip `@ro_chip` (Read-only, Duplicate to Edit).
  The panel has `@ghash_orig` and `@ghash_cur` (click copies the full glyph hash, `assert clip <text>` checks the clipboard) and `@glyph_find`.
- Layers: `assert l.count`, `l.cur` (index of the "Current glyph" row), `l.sel`, `l.pop` (the Add Layer popover is open), `l.focus`, `l.fonts` (fonts in the layer glyph cache) and, per layer N from the top: `l.vis.N`, `l.op.N` (opacity %), `l.dx.N`, `l.dy.N`, `l.color.N` (`0xRRGGBB`), `l.same.N` (follows the edited glyph), `l.this.N` (from this font), `l.code.N`, `l.ink.N` (inked pixels, -1 = not in font).
  Widgets: `@+_Add_Layer`, `@lay_row_N`, `@lay_eye_N`, `@lay_up_N`, `@lay_down_N`, `@lay_del_N`, `@lay_sw_K` (swatches), `@lay_hex`, `@lay_op` (slider), `@lay_opv.value|plus|minus`, `@lay_x.*`, `@lay_y.*`; in the popover `@lay_src_This_font`, `@lay_src_Library_font`, `@lay_search`, `@lay_font_N`, `@lay_same`, `@lay_glyph`, `@Add`, `@Cancel`.
- `assert w.<widget> 0|1`: a widget of that name was drawn in the last frame (for the hidden rows).
  `assert pixel <target> [dx dy] RRGGBB` reads the screen.
  `$GHASH` and `$GOHASH` in a script are the pxgl hash of the glyph being edited, now and when the font was opened.
  A value may be written `0xRRGGBB`.
- `setdialog folder|pxfdb|save <path>` overrides the native dialogs (`none` clears them): the folder pickers of Import and Export, the Load dialog, and the Save dialogs of Save As, the import target and the backup.
- File menu and export dialog: `@File`, `@file.load`, `@file.save`, `@file.saveas`, `@file.import`, `@file.export`, `@file.quit`, `@Save`; `assert filemenu`; the dialog has `xdopen`, `xfound_fonts|typefaces|glyphs`, `xbackup`, the widgets `@xd.backup`, `@xd.change`, `@Append`, `@Overwrite`, `@Cancel`; after an export `xw_*` (written), `xs_*` (skipped), `xd_*` (deleted) and `xbackupfailed`.
- `copyfile`, `writefile`, `delete`, `verifylib <pxfm> <libroot> [N]`, `shot <png>`, `expectquit`.

Command line options for the console build: `--ini <file>`, `--font <hash>`, `--typeface <hash>`, `--duplicate`, `--import-to <pxfdb>`, `--job-hold-ms N` (keep the progress screen at least that long), `--shot-job <png>` with `--shot-job-ms N` (a screenshot of the progress screen), `--play <script>`.

The environment variable `PXFE_TRACE=1` prints one `TRACE load ...` or `TRACE import ...` line with the time of every load or import, and `TRACE saveas ...` for Save As.

## Scripts

- 00 to 06c: read-only original, grid, metrics, selection, delete, clone, glyph editor, palette, font without ASCENT/DESCENT.
  06b and 06c and 02 export to a folder to read the pxfm that a commit made.
- 07a: Save of a duplicate (file bytes change only at Save, the rows, the replaced second commit), 07b reload; 07d a real font imported from `tests\tmp\reallib`; 07e the empty state, File menu, Load of a missing and of a broken file.
- 08a to 08d: unsaved-changes prompt on close (Cancel, Esc, Discard, Save); 08e on Load (Cancel, Discard, Save, Esc, an error).
- 09: input paths.
- 10: read-only behaviour and Duplicate to Edit.
- 11: modified marker by hash.
- 12: typeface duplicate and Save.
- 13: default glyph and kerning.
- 14: browser: tabs and counts, filter, sample, zoom, columns, opening a card read-only, Back, Duplicate to Edit, the commit into memory, Save.
- 15: Import Folder from the File menu (progress, replace prompt, the folder untouched, navigation).
- 16: Load (File menu, Ctrl+O), the file only read.
- 17 and 17b: duplicate, edit, Save; reload.
- 18 and 18b: Save As, later Saves go to the new file, replace prompt, `lastPxfdb` in the ini; reload.
- 19: uncoded glyphs: `#0` captions, delete renumbers, Clone with "No Code Point".
- 21: the Similar tab, glyph usage, Find in Library, the glyph finder.
- 23: modified marker (title, status bar, Save button) and the prompts of Import and Quit.
- 24: Import with nothing loaded, a folder without data, a broken file (nothing written), a failing import that keeps the loaded database.
- 25: Export to an empty folder (no dialog, byte equal files, summary).
- 26: Append (existing file and other files untouched, new font committed and written).
- 27: Overwrite with a backup (backup holds the old data, other files survive, folder equals memory).
- 28: Overwrite when the backup fails (nothing deleted, second confirmation) and with the backup switched off.
- 29: a duplicate committed several times leaves one font in memory; editing back to the original drops it.
- 30 and 30b: delete a font and a typeface, Save, reload.
- 40 to 45: the glyph view (bar, panel, layers); 45 is the discoverability: "Open in Glyph View" in the font view, the bar's `@Layers` toggle (`@Layers_1` when a layer exists), the empty Layers tab with its big `@+_Add_Layer`.
- 50 to 55: samples, typeface boxes, delete requests of the browser.
- 58: the red Delete Font and Delete Typeface buttons of the browser's side panel, end to end (confirmation, Cancel, rewritten typeface, emptied typeface, orphaned glyphs, the Fonts tab).
- 49, 56, 57, 90, 91, 92 (not in `run_all.bat`): screenshots and a performance run; start them with a database as the first argument.
- 20: smoke test on the real library (see below).

## Smoke test on the real library

`tests\e2e\run_reallib.bat` runs `20_reallib_smoke.txt` twice: loading `pixelglyphlib\pxfdb.pxfdb`, and importing the folder `pixelglyphlib` into `tests\tmp\real.pxfdb` (the import takes minutes).

`run_reallib.bat db` or `run_reallib.bat import` runs only one of them.

`pixelglyphlib` is only read.
The script's Save As goes to `tests\tmp\real_saveas.pxfdb`, and `PXFE_TRACE` prints the load, import and save times.

It opens five fonts as originals, one after the other: a FON typeface font with RATIO, a font without ASCENT and DESCENT, the font with the most glyphs, and two more.
For each one it asserts read-only, not modified, and a current hash that equals the original hash and names a font of the store.
Then it makes a duplicate, makes no edit, and asserts that Save has nothing to write.

The fonts are chosen by hash, so the script depends on those fonts being in the library.
