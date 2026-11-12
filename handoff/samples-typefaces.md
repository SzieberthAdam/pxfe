# Handoff: samples and typeface grouping

## Goal

DESIGN.md Version 4, subsections "Samples", "Library: typefaces" and "Deleting fonts and typefaces".
The shared multi-line sample, the font-view sample strip, the multi-line browser sample and the typeface boxes in the Typefaces tab.
The browser delete requests (Del key and context menu) are included.

## Done

- src/sample.c and src/sample.h are new.
- They hold the shared sample setting (pxfe.ini `[app] sample=`, migration from `[browser] sample`, change counter, debounced flush).
- They hold the layout, render and cached draw functions (Sample_Layout, Sample_Render, Sample_Draw, Sample_Measure).
- They hold the multi-line editor widget (SampleEdit_Do) and the font-view strip (SampleStrip_*).
- src/browser.c and src/browser.h group the Typefaces tab into boxes, with a header and variant rows.
- Filters act per variant, and keyboard navigation works across headers and variants.
- The list is virtualized with variable-height boxes.
- The context menu has Open, Duplicate to Edit, Delete Font and Delete Typeface.
- BROWSER_DELETE_FONT (5) and BROWSER_DELETE_TYPEFACE (6) are new BrowserAction values.
- The selection moves to the next item after a deletion.
- New hooks: Browser_BoxCount, Browser_BoxVariants, Browser_SelectedIsHeader, Browser_TypefaceHeaderCenter, Browser_CardInkLines, Browser_LastAction, Browser_LastPxfm, Browser_LastPxtf, Browser_FrameStats.
- src/main.c has small edits: Sample_LoadIni, the early return in HandleShortcuts while a sample is focused, and the strip area.
- src/play.inc has new targets, states, asserts and commands (`@hdr:N`, bboxes, blast, samplelines, stripzoom, setclip, show and others).
- The measured frame time on the real library (1185 variants) is about 4 to 6 ms on average and at most 16 ms while scrolling (debug -O1 build).

## Not done

- The end-to-end deletion handler is not mine. Browser only returns the BROWSER_DELETE_* action.
- The editor preview (src/editor.c) does not use sample.c yet.
- The full run_all.bat was not completed in this session.
- build.bat test and the older scripts 14_browser and 21_usage_finder were not rerun against my changes after the last build.
- The remaining screenshots (header_selected, strip, strip_tall, strip_collapsed) were not reviewed.

## Build and test state

- `build.bat` ended with: `cannot open output file pxfe.exe: No error`, `ld returned 1 exit status`, `Build failed`.
- This is a link error, not a compile error; pxfe.exe is most likely locked by a running process.
- run_all.bat failed the same way on tests\tmp\pxfe_e2e.exe.
- `build.bat test` ended with: `171 passed, 0 failed`.
- Earlier, with the real exe, these scripts passed with 0 failures: 50 (29), 51 (24), 52 (15), 53 (7), 54 (61), 55 (15).

## Known bugs and risks

- Scripts 51 and 53 run in sequence in run_all.bat without a fresh library between them.
- Other agents changed the store API in main.c mid-work, so a stale exe can cause phantom failures (for example blastpxfm showing the pxtf hash).
- Frame times were measured on a debug build only.

## Next steps

- Close any running pxfe.exe or pxfe_e2e.exe, then run `build.bat` and check for warnings.
- Run `tests\e2e\run_all.bat`, or run 14_browser and 21_usage_finder individually first.
- Fix anything the grouping broke in the older browser scripts.
- Review the remaining screenshots in tests\shots.
- Connect the delete actions to the storage handler.

## E2E scripts

- Added: tests\e2e\50_sample_browser.txt, 51_sample_strip.txt, 52_sample_strip_live.txt, 53_sample_persist.txt, 54_typeface_boxes.txt, 55_delete_requests.txt.
- Added, not in run_all: 56_shots_v4.txt and 57_perf_typefaces.txt.
- Changed: tests\e2e\run_all.bat and tests\e2e\README.md.
- No older script was changed.

## Review pass (2026-11-12)

The items of this handoff were checked in the real UI on a copy of the real library (`tests/tmp/b/real.pxfdb`), with screenshots.

### Found missing

- Deletion could only be started by `Del` and a right-click menu; the side panel had no visible control.
- The Typefaces tab badge counted variants (1075), not typefaces (1020); the panel said "13 of 1075" with no unit.
- The toasts covered the bottom of the browser's side panel.

### Changed

- `src/theme.c`, `src/theme.h`: `Th_ButtonDanger`, a button with a red label and border.
- `src/browser.c`, `src/browser.h`: "Delete Font" and "Delete Typeface" buttons in the pinned selection block (a header selection shows only the typeface one); the block is 146 px high so the sort control stays visible at 900 px; the tab badge counts typefaces (`Browser_TypefaceTotal`); the panel and the status line name variants and typefaces.
- `src/main.c`: the toasts stay left of the browser's side panel.
- `src/play.inc`: state `btypefaces`.
- `tests/e2e/58_delete_ui.txt` (in `run_all.bat`): the buttons end to end; the earlier deletion path (AskDelete, store plan, rescan, next selection) was already correct and is also covered by 30 and 55.

### Verified

- The multi-line sample in the browser cards (Typefaces and Fonts tabs, zoom 2) and in the font view's strip (zoom, expand, resize, collapse) looks right.
- Typeface boxes with 12 variants, header selection and variant selection look right.
- 14_browser and 21_usage_finder pass; 51 and 53 share only `e2e.ini` on purpose (neither writes the library), so no fresh library is needed between them.
- Deleting a font of a typeface with several variants rewrites the typeface (new pxtf, old one gone); deleting the last variant's font or a typeface removes the emptied typeface; orphaned glyphs go.

