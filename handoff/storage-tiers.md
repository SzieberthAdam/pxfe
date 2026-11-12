> **Superseded by DESIGN.md "Version 5: Load, Save, Import, Export".**
> The tiers, the pipeline and the drafts described here are gone; the deletion cascade and the hash labels stay.

# Handoff: storage tiers, save pipeline, deletion

## Goal

Implement the storage tiers of DESIGN.md "Version 4" ("Where the work goes: Memory, PXFDB, ...", "Hashes", "Deleting fonts and typefaces") and "Version 3 / The store".

The three tiers are Memory, the PXFDB file and the static folders pxgl/pxfm/pxtf.

The third tier is labelled "PXTF-FM-GL" everywhere in the UI (button, status text, tooltips, badges, prompts).

The toolbar pipeline is `[Save to Memory] > [Save to PXFDB] > [Save to PXTF-FM-GL]`.

Deleting a font or typeface happens in Memory first and travels through the pipeline, with confirmations.

## What is done

### Store layer (src/store.c, src/store.h, src/store_int.h, src/store_sim.c)

- Sorted hash sets per kind (glyph, pxfm, pxtf) for `mem`, `pdb`, `sta` and `del`.
- Pending counts: `store_pending`, with PDB_ADD, PDB_DEL, STA_ADD and STA_DEL rules.
- `store_save_to_memory` (renamed from `store_save_font`, no persistence any more).
- `store_flush_to_pxfdb`: creates the file with the full schema (VACUUM INTO) or does INSERT OR IGNORE plus DELETEs in one transaction.
- `store_export_to_files`: never overwrites, checks that the text hashes to its name, deletes files only for hashes deleted in Memory.
- Open logic: `store_open_pxfdb`, `store_open_root_ex`, `store_set_files_root`.
- A root with pxfdb.pxfdb loads it and reads only the missing files.
- A root without one is built, and everything is then pending for the PXFDB.
- The cache dir is dropped (`--cache` is still accepted and ignored).
- Deletion: `store_plan_delete_font`, `store_plan_delete_typeface` (dry-run plan with summary) and `store_apply_delete`.
- The cascade rewrites typefaces (variant numbers keep gaps), deletes empty typefaces, and deletes newly orphaned glyphs only.
- `store_sim_fonts_removed` in store_sim.c keeps the similarity cache consistent.

### UI (src/storebar.c, src/storebar.h, src/main.c, src/panel.inc, src/play.inc, src/winfile.c)

- storebar: the pipeline widget with flat buttons, chevrons, badges (full and compact), tooltips, and the status sentence.
- main.c: `SaveToMemory`, `SavePdb`, `SaveFiles`, `FlushNow`, `ExportNow`, `MemoryPrompt`, `MemoryAhead`, `AskDelete`, the toolbar and overflow menu, the status bar tier text, the quit and open-library prompts, and the BROWSER_DELETE_FONT / BROWSER_DELETE_TYPEFACE handler.
- Ctrl+S is Save to Memory, and Ctrl+Shift+D is Save Draft.
- The draft path is shown in the toast and the status bar, and "Open Folder" appears for drafts (`Win_OpenFolder`).
- After Save to Memory the browser is rescanned and the font view shows the saved font as the new read-only original.
- Hash labels: the info bar shows the short hash, and the right panel has full-hash boxes (`PanelHashBox`) with click-to-copy.
- play.inc: new asserts canmem, canpdb, canfiles, pdb_*, files_*, pdbexists, filesroot, readfonts, readglyphs, readtypefaces, tier and statusdoc.
- `cansavelib` now maps to `SaveToMemoryAllowed`.

### Tests

- tests/test_store.c rewritten for all diff cases and deletion cases.
- tests/test_pxtf.c, make_testdata.c, insight_demo.c and test_library.c were adapted to the new signatures (test_library.c has not been run).

## What is not done

- No new e2e scripts (30_*.txt to 39_*.txt) were written.
- The older e2e scripts were not updated for the new semantics.
- No screenshots tests/shots/v4_tiers_*.png were taken, so the look is unreviewed.
- The real-library smoke (read-only open of pixelglyphlib, timing, only the 4 extra files read) was not run.
- `build.bat libtest` was not run.
- tests/e2e/README.md and run_all.bat were not updated (README still mentions Save to Library, `--cache` and storekind 2).
- Lastly, `lastPxfmPath` and `lastPxtfPath` in main.c are cleared by `SaveToMemory` and never set again, so `$SAVEDPXFM` and `$SAVEDPXTF` in scripts are empty.
- Setting them in `ExportNow` (path of the exported pxfm and pxtf under the static root) is the planned fix.

## Build and test state

`cmd //c ".\build.bat"` result:

- `C:\raylib\w64devkit\bin/ld.exe: cannot open output file pxfe.exe: No error`
- `Build failed`
- The cause is a running pxfe.exe (process 15732, not started by this work), which locks the output file. Compilation itself produced no warnings or errors.

`cmd //c ".\build.bat e2e"` result: `Build OK: tests\tmp\pxfe_e2e.exe`, with no warnings.

`cmd //c ".\build.bat test"` result lines:

- `136 passed, 0 failed` (pxfont)
- `101 passed, 0 failed` (pxtf)
- `171 passed, 0 failed` (store)
- The line `error was: ...00000000.pxgl: the hash of its content ...` is an expected message of a passing test.

`tests\e2e\run_all.bat` was run once.

- 00_smoke passes.
- It was stopped by the hand-off request before finishing, so only partial results are known.
- Failures seen so far are from scripts that expect the old Save to Library behaviour: 02_metrics (lines 249 to 258), 06b_palette, 06c_noascent and 07a_save.
- 02_metrics also fails at lines 113, 114 and 117 (m.capheight and a pixel assert), which may be unrelated to this work and needs a look.

## Known bugs and risks

- Flush and export run synchronously on the UI thread, so a large first export blocks the UI.
- The browser's own status line is overpainted by main's status bar.
- A browser hook is needed if the browser agent wants extra status content.
- After Save to PXFDB without exporting to the folders, reopening resurrects a deleted font from the folders. This is by design, since the deletion has not reached the folders.
- Toolbar fit at the minimum 1000 px window width is unverified (the compact badge level is meant to handle it).
- All UI behaviour (prompts, deletion dialogs, screenshots) is unverified in a running app.

## Exact next steps

1. Close the running pxfe.exe, then run `cmd //c ".\build.bat"` to confirm the main build links.
2. Set `lastPxfmPath` and `lastPxtfPath` in `ExportNow` in src/main.c.
3. Update the old e2e scripts.
   - `key s ctrl shift` becomes `key s ctrl`, then `click @Save_to_PXFDB`, then `click @Save_to_PXTF-FM-GL` wherever files are asserted.
   - `Save_to_Library` clicks become the same sequence.
   - Candidates: 02, 06b, 06c, 07a to 07e, 08a to 08e, 10, 11, 12, 14 to 18b, 20.
   - `storekind 2` no longer exists.
   - Drafts now use Ctrl+Shift+D, and `key s ctrl` no longer writes a draft.
   - 07a also needs the "Unchanged" toast text changed to "Nothing new".
4. Write new scripts 30_*.txt to 39_*.txt.
   - Cover tier transitions with enabled and disabled states, open-with-diff on copies, the quit and open prompts, and the draft path display.
   - Cover the delete cascades (2-variant typeface, last variant, a font shared by two typefaces, shared glyph stays, pre-existing orphan stays).
   - Flush the deletions to a copied pxfdb and copied folders with SQL and byte-compare asserts.
5. Add the new scripts to tests/e2e/run_all.bat, drop `--cache`, and update tests/e2e/README.md (Markdown protocol applies).
6. Take screenshots `tests/shots/v4_tiers_*.png`, read them and refine storebar.c.
7. Run the real-library smoke read-only and time it, and run `build.bat libtest`.

## E2E scripts added or changed

- None added.
- None changed.
- Helper scripts for running single e2e scripts are in the scratchpad (`fresh.sh`, `run1.sh`), not in the repo.
