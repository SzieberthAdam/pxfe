## Goal

DESIGN.md Version 4: Glyph view, Layers, Hashes (UI in English).

The glyph view hides the font view's second toolbar row and the read-only band.
The editor bar gets zoom, a Metrics toggle and a "Read-only · Duplicate to Edit" chip.
The right panel has the tabs Glyph, Usage and Layers.
Layers are a session-only stack of reference glyphs (this font or another library font) drawn pixel-perfect under or over the edited glyph.

## Done

New files:

- src/layers.h and src/layers.c hold the layer stack, font cache, Layers tab cards and the Add Layer popover.
- src/gpanel.inc holds DrawGlyphPanel (tabs Glyph, Usage, Layers; full original and current glyph hash, click copies; Find in Library).

Changed files (small localized edits in shared ones):

- src/editor.h and src/editor.c: EDITOR_DUPLICATE result, Editor_OriginalHash, Editor_Layers, Editor_ShowMetrics, Editor_CurrentHidden, two-row toolbar with zoom and chip, layer drawing, Alt+drag nudges the selected layer, editing locked while the current glyph is hidden.
- src/main.c: layers.h include, glyphTab, editor frame layout (edFrame, edArea), Layers_SetStore, EDITOR_DUPLICATE handling, popover drawing.
- src/panel.inc: one line that forwards to DrawGlyphPanel when in the editor.
- src/play.inc: hooks l.*, w.*, gtab, emetrics, ehidden, assert clip, Subst $GHASH and $GOHASH, 0x values.
- tests/e2e/run_all.bat and tests/e2e/README.md list the new scripts.

## Discoverability (added later)

- The glyph view's bar has a "Layers" toggle (`@Layers`, labelled "Layers N" with N extra layers); `Editor_SetLayersTabActive` and `Editor_TakeLayersRequest` connect it to `glyphTab` in main.c.
- The empty Layers tab (`Layers_DrawPanel`) shows an explanation and a big "+ Add Layer" button.
- The font view's Glyph tab has "Open in Glyph View" (Enter).
- The Add Layer popover moved down so it no longer covers the bar's second row.
- Script 45_layers_discover covers all three; it is in run_all.bat.
- The v4_glyph_*.png screenshots were regenerated (script 49) and looked at.
- Seen and left: the Usage tab of a changed glyph that is in no font shows "0 x 0 px" in its header block (Insight, not the glyph view).

## Spinner hold-repeat question (answered)

It is not a regression and not a play quirk.
The first edit of a duplicate adds the "FONT HASH (ORIGINAL)" box to the Font tab, which moves the metric rows down under the mouse that stays put, so the held button is no longer under it.
After a click that makes the first edit, the hold repeats.
09_misc keeps the click first and says why.

## Not done

- The glyph-view preview strip is not switched to the shared multi-line sample setting.
- The chosen glyph tab is not persisted in the ini (by design).

## Build and test state

`build.bat e2e` compiled and linked.
`build.bat` printed: "ld.exe: cannot open output file pxfe.exe: No error", "Build failed" (pxfe.exe is most likely locked by a running instance; not a compile error).
`build.bat test` printed: "171 passed, 0 failed".
My scripts against the real e2e build: 40_glyph_bar 46/0, 41_glyph_panel 36/0, 42_layers_add 74/0, 43_layers_edit 56/0, 44_layers_pixels 39/0, 06_editor 119/0, 09_misc 25/0, 10_readonly 26/0, 21_usage_finder 68/0, 19_uncoded 32/0.

## Known bugs and risks

- The pinned grey pixel 8F8F91 in 44_layers_pixels may depend on the GPU.
- Scripts 02, 06b, 06c, 07a-d, 12, 14, 17, 17b, 18, 18b, 50, 52 and 55 failed earlier on a snapshot build because of other agents' save-pipeline and sample work, not rechecked.
- Layers are not saved; the font hash never changes through layers (asserted).

## Next steps

1. Close any running pxfe.exe and run `build.bat`, confirm it is warning-free.
2. Run `tests/e2e/run_all.bat` and check the failures listed above against their owners.
3. Run `tests/e2e/49_shots_glyph_v4.txt` (not in run_all) and review the v4_glyph_*.png screenshots.
4. Switch the glyph-view preview strip to the shared sample setting.

## E2E scripts

Added: 40_glyph_bar, 41_glyph_panel, 42_layers_add, 43_layers_edit, 44_layers_pixels, 49_shots_glyph_v4 (screenshots only).

Changed: 06_editor (zoom pinned to 9 before the advance section), 09_misc (metric-field part moved to the font view, hash toast text "Font hash copied", a click before the spinner hold).

Helpers under tests/tmp (snapbuild.sh, run1.sh) are private and not deliverables.
