# pxfe handoff (2026-11-12)

Version 5 (Load, Save, Import, Export) is implemented.
The design is in [DESIGN.md](DESIGN.md); its last section, "Version 5: Load, Save, Import, Export", replaces the storage tiers, the save pipeline and the drafts of version 4.
The glyph view, layers, samples, typeface boxes and hash labels of version 4 stay as they were.

## State

- `build.bat e2e` builds with no warnings.
- `build.bat test` passes: 136 pxfont, 101 pxtf and 154 store checks, 0 failed.
- `tests\e2e\run_all.bat` passes: every script in the list of `run_all.bat`, 0 failed.
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
