# pxfe: pixel font editor

> This file describes the current design only, by topic, without version history.
> It must stay under 100,000 characters (`wc -m DESIGN.md`); when adding, shorten something else.

pxfe is a desktop browser and editor for the pixel fonts of a pixelglyphlib library: pxtf typefaces, pxfm fonts, pxgl glyph files and hex palettes, kept in one pxfdb SQLite database.
It is written in C99 with raylib 6 and SQLite 3.54 (`vendor/sqlite3/`, static).
The formats are specified in `C:/Users/jnsGEzD3/Documents/GitHub/pixelglyphlib/spec/`: `pxfm.md`, `pxtf.md`, `pxgl.md`, `hex.md` and `pxfdb.md`; pxfe follows the spec of pxfm version 3, as in the working tree after commit 5dd97ca2c4: codes `G+CCCCCC-VVV` with variants, and the glyph line `GLYPH code advance x y [glyph [palette]]`.
The whole UI is in English.

## Build

The toolchain is the w64devkit GCC bundled with raylib in `C:\raylib`.

- `build.bat` builds `pxfe.exe` (GUI subsystem).
- `build.bat debug` builds it with a console and debug info.
- `build.bat test` builds and runs the core unit tests.
- `build.bat e2e` builds the scripted end-to-end test binary; `tests\e2e\run_all.bat` runs the scripts.

All `src/*.c` files are compiled into the app, so a new module is just a new file in `src/`.
`vendor/raygui.h` is included with `RAYGUI_IMPLEMENTATION` in exactly one file, `src/main.c`; no raygui widget is used.
The tests are described in `HANDOFF.md` and `tests/e2e/README.md`.

## Modules

| File | What it does |
| --- | --- |
| `src/pxfont.h/.c` | font model, SHA-256, canonical form, pxfm v3 and its codes, pxgl and hex read and write, the per-glyph palette table, drafts; no raylib |
| `src/pxtf.h/.c` | typeface model, pxtf read and write; no raylib |
| `src/store.h/.c`, `src/store_int.h` | the SQLite store in memory: load, import, save, export, change counter, rows and model both ways, lists, deletion with cascade, editing in place, glyph usage; no raylib |
| `src/store_sim.c` | similar fonts and typefaces; no raylib |
| `src/library.h/.c` | the browser index from the store, lazy threaded loading; no raylib |
| `src/fsettings.h/.c` | the settings sidecar `<db>-settings`; no raylib |
| `src/theme.h/.c` | the design system: colours, fonts, components, drawing helpers |
| `src/colorpick.h/.c`, `src/colormath.h/.c` | the colour picker and its colour models (colormath: no raylib) |
| `src/browser.h/.c` | the library views (Typefaces, Typeface, Fonts) |
| `src/finder.h/.c` | the Glyphs view |
| `src/insight.h/.c` | Glyph usage, Similar fonts and Similar typefaces, on a worker thread |
| `src/sample.h/.c` | the shared sample text, its renderer and editor, the sample strip |
| `src/main.c` | window, app state, views, the top bar and Menu, Load / Save / Import / Export flows and dialogs, commits, locks, glyph grid, status bar |
| `src/panel.inc`, `src/gpanel.inc` | the right panels of the font view and of the glyph view |
| `src/editor.h/.c` | the glyph view |
| `src/selection.h/.c`, `src/raster.h/.c` | the selection mask and the cell paths of the tools; no raylib |
| `src/layers.h/.c`, `src/flayers.h/.c` | session layers with the Layers tab and Add Layer popover; font layers |
| `src/outline.h/.c`, `src/bmsrc.h/.c` | outline and bitmap font sources |
| `src/fit.h/.c`, `src/fitdoc.inc` | fitting an outline to pixels, importing a font file, glyphs that follow a layer |
| `src/sheet.h/.c`, `src/sheetref.h/.c` | finding a font in a picture, and its reference glyphs |
| `src/winfile.h/.c` | native Windows file and folder dialogs |
| `src/play.inc` | the scripted input player of the end-to-end tests (`--play`) |

## Command line

`pxfe.exe [<file.pxfdb> | <folder>] [--font <hash>] [--typeface <hash>] [--unlock] [--import-to <file.pxfdb>]`.

- A `.pxfdb` file is loaded.
- A folder starts the import flow: the target file is asked for (or given with `--import-to`), the folder is imported, the new database is loaded.
- `--font` and `--typeface` open that font or typeface; `--unlock` then unlocks the font.
- With no argument, the last database (`lastPxfdb` in `pxfe.ini` beside the exe) is loaded, else an empty state with "New Database", "Load Database…" and "Import Folder…".
- New Database (also in the Menu, after the unsaved-changes prompt) starts with a blank page: an empty database in memory, titled "Untitled", with no file, and the New Font dialog opens on it.
  Its first Save is a Save As; until then nothing is written and `lastPxfdb` does not change.
- After a database is loaded (at start or by Load), pxfe shows the Typefaces view.

## Design system

The look is that of FontLab 8: quiet, flat, precise.
One component is one function: every use site of a role calls the function of its owner in `src/theme.c` (declared in `theme.h`), and a view never draws a second variant.
There is one component per kind of control, reused everywhere: a new kind of control (such as the colour picker) is added to the catalogue once, with a clean API, and every view that needs it uses it.
The measure of success is that the user interface code shrinks.
No view code holds a literal font size or colour: both come from `theme.h`.
A component conversion is done when every use site calls the owner, the bespoke code is deleted, the UI files have not grown, the builds have no warnings, the end-to-end scripts pass, and the screenshots of every view showing it were looked at.

### Typography

Noto Sans Regular, Noto Sans SemiBold and GNU Unifont, loaded from `assets/fonts` (the user rejected system fonts for a font program).
Exactly three UI sizes (18, 16, 13) and the pixel face at 16:

| Role | Face | Size | Colour | Function |
| --- | --- | --- | --- | --- |
| Heading (view title, dialog title) | Noto Sans SemiBold | 18 | `TH_TEXT` | `Th_TextHead` |
| Body: label, value, button, tab, row text | Noto Sans Regular | 16 | `TH_TEXT` (label: `TH_TEXT_DIM`) | `Th_Text` |
| Caption (section title) | Noto Sans SemiBold | 13 | `TH_TEXT_DIM`, upper case | `Th_Section` |
| Small: status line, hint, footnote | Noto Sans Regular | 13 | `TH_TEXT_DIM` | `Th_TextSmall` |
| Hashes, code points, glyph captions | GNU Unifont | 16 (32 for a large preview) | `TH_TEXT` | `Th_TextUni`, `Th_CodeDraw` |

- The sizes are `TH_SIZE_HEAD`, `TH_SIZE_BODY`, `TH_SIZE_CAP`, `TH_SIZE_SMALL`.
  A size is the em, as in CSS: Noto Sans is loaded at `em * 1.362` (`TH_NOTO_RATIO`) because raylib's `LoadFontEx` size is the ascent-to-descent height.
- Fonts are found under `assets/fonts` next to the executable, else two folders up, else the working directory; a missing file raises an error toast and raylib's default font stands in.
- Unifont is drawn only at 16 px or 32 px, with point filtering and whole-pixel positions.
  Both `.otf` files stay in memory and a glyph is rasterised with `LoadFontData` when first drawn, into a 1024 x 1024 atlas of 32 x 32 slots that is flushed when full; planes 1 to 16 come from `unifont_upper`.
- At 16 px every coverage value of the Unifont `.otf` outlines is 0 or 255, so no atlas had to be made by hand.
- Glyph captions (`Th_CodeDraw`, `Th_TextRich`): the character itself in Unifont, centred; "U+XXXX" while the mouse is over the caption or its cell, and always for a code point with no visible Unifont image (controls, blanks, marks without an advance, code points Unifont lacks); "#n" for a glyph without a code point; a variant adds its "-VVV" to the text and shows no image.
  Sites: the font view's cell captions, the glyph segment of the navigation bar, the glyph view's bar, the layer rows, the Glyph usage rows, the code beside Find in Library, the Add Glyph dialog.
- A glyph preview (the large character of the Glyphs view) is Unifont at 32 px, never another size.
- Roles differ by size, weight and colour, never by a size of their own: a new role takes one of the five rows above.
- The side panel is 352 px wide and the minimum window is 1110 x 600; at that size and at 1400 x 900 no text is clipped or overlaps without an ellipsis.

### Spacing

- The grid is 4 px: paddings 4, 8, 12; the gap between controls is 8.
- Control height is `TH_ROW_H` (28); a list row is at least 28.
- A panel has 12 px padding (`TH_PAD`); a section caption takes 24 px.
- A button's width is its text plus `2 * TH_PAD`.
- Radius `TH_RADIUS` (4 px), borders 1 px through `Th_RectLines`; no gradients, no heavy borders.
- Glyph pixels are exact integer-scaled rectangles.
- The wide controls of the library View panel take a line of their own below their label (`wide_row`).

### Component catalogue

A use site that draws the look itself, or calls the owner with a look of its own, is a defect.
States are normal, hover, active, disabled, and focused where the component takes input.

#### Stepper

- `Th_Stepper(r, value, prefix, flags)`: `[-] 3x [+]`, `TH_ROW_H` high, `TH_STEP_W` (96) wide for a zoom; `TH_STEP_NO_MINUS` and `TH_STEP_NO_PLUS` at the limits; `TH_STEP_EDIT` is a number field being typed into.
- It acts on the press; a held button repeats after 0.45 s.
- It is every zoom, the advance, the metric number fields and the layer offsets; there is no "Auto" zoom.
- Registers `<prefix>-` and `<prefix>+`.

#### Hash display

Exactly two forms, both in Unifont; nothing else prints a hash.

- `Th_HashShort(kind, hash, x, y, flags, copyName)`: a dim kind word (`Font`, `Typeface`, `Glyph`, or none) and the first 8 characters.
  `TH_HASH_ACCENT` marks a changed state, `TH_HASH_RIGHT` right-aligns, `TH_HASH_MEASURE` only measures, `TH_HASH_SMALL` fits a line of small text; with `copyName` a click copies the full hash.
- `Th_HashField(x, &y, w, caption, hash, name, what, flags)`: a caption and a 28 px box with the full hash, never wrapped or cut, on the panel's margins (16 px, as the caption and the rows); a click copies it with the toast "`what` hash copied"; an empty hash reads "no ink", a NULL hash "not known".
- The clipboard, the toast and the hint live in `theme.c` (hooks `Th_HoverFn`, `Th_NoticeFn` set by `main.c`).
- A font or typeface without a name shows its short hash; a name is never built from a hash.
- The short form is used by cards, rows, headings, the selection block, the info bar, the status bar, the glyph bar, and the Usage, Similar and Layers lists; the full form by the Font, Typeface and Glyph tabs and the glyph view's Glyph tab.

#### Buttons

- `Th_Button(r, label, flags, hint)`; `Th_ButtonW(label)` is its natural width.
  `TH_PANEL_ALT` fill, `TH_LINE` border; hover `TH_PANEL` with `TH_HOVER_BORDER`; pressed `TH_PRESSED_FILL` with accent border and text; active `TH_ACCENT_TINT` with accent border and text; disabled dim and inert.
- Flags, the only variations: `TH_BTN_DISABLED`, `TH_BTN_DANGER`, `TH_BTN_PRIMARY`, `TH_BTN_ACTIVE`, `TH_BTN_ICON(TH_IC_x)`.
- Primary (accent fill, white text) is the one main action of a surface, such as a dialog's confirming button; a surface never shows two primary buttons for different commands.
- An icon button has no fill or border at rest; its label is only its script name, its `hint` the tooltip.
- `hint` goes to the view through `Th_Tip` while hovered, also when disabled.
- Danger (`Th_ButtonDanger`): `TH_DANGER` label and border; with primary a `TH_DANGER` fill; for every delete command and only those.
- Lock: the macro `Th_Lock(r, locked, hint)` with `TH_IC_LOCK` or `TH_IC_UNLOCK`, script name `@Unlock` while locked and `@Lock` while unlocked; every command that makes something editable is this button.
- Toggle: `Th_Toggle(r, label, &on, flags, hint)`, active while `on`; true when it changed.
  Used by Metrics, Captions, Checker, Filled, Pixel-perfect, Contiguous, 8-way and the sample presets.
- A button registers its label (the text before two spaces); no view draws a button of its own.

#### Segmented control

- `Th_Segmented(r, &(ThSeg){ labels, n, sel, disabledMask, prefix, weights, hints })`: one look for the navigation bar, panel tabs, sort selectors and value choices: a `TH_TRACK` track, the selected segment raised with accent text.
- `weights` are relative widths (none: equal).
- Segment i registers `<prefix>_<label>` and `<prefix>.<i>`.

#### Text field, checkbox, switch

- `Th_TextField`: 28 px, `TH_PANEL` fill; focused has the accent border and a caret; a search field adds a placeholder and a clear mark.
  Enter commits, Escape reverts.
- Text selects as in any editor: a drag, a double click (a word), Shift with the arrows, Home and End, Ctrl+A; Ctrl+C, Ctrl+X and Ctrl+V act on the selection, and typing replaces it.
- `TH_TF_READONLY`: the field takes the focus, selects and copies, but takes no edits (a locked field).
- `Th_Check` is for a setting in a form, `Th_Switch` for an on/off property; both carry their label and share one geometry helper.

#### Section caption

- `Th_Section(title, x, &y, w, right)`: semibold 13, dim, upper case, a hairline to the right, 24 px high; `right` is a small dim count at the end.
- Every caption folds (chevron, hover plate, registered as `<Title>_Fold`) and returns whether it is open; the caller draws the content only then.
  The state is kept per title for the session (`Th_SectionOpen`, `Th_SectionSetOpen`); the sample strip keeps its own in pxfe.ini.
- Used by every panel (the library side panel, the Font, Glyph and Usage tabs, the Glyphs view's panel, the Layers panel and its Add Layer popover, the Usage and Similar sections and the sample strip as fold headers) and by the full-hash field.
- A panel never writes its own heading look (dialog and view titles are `Th_TextHead`); inline toolbar labels (MOVE, ADVANCE) use `Th_TextCap`, the primitive under it.

#### Label/value row

- `Th_Row(x, &y, w, label, value, flags)`: the dim label in a `TH_ROW_LABEL_W` (108) column, the value right-aligned with an ellipsis; `TH_ROW_UNI` for Unifont.
- With `value` NULL it returns the rectangle where the caller draws a control (text field, dropdown, segmented control, stepper or number box); a control row is `TH_ROW_H` plus half a gap high.
- Used by the Font, Typeface and Glyph tabs, the font view's selection block, the browser's Style, Format, Colours, Zoom, Columns and Sort by rows, the Glyphs view's Zoom and Sort by, and the Layers cards (Opacity, Offset X, Offset Y).
- Not rows: switches and checks, the status line, and the pinned selection block of the browser.

#### List row and card

- `Th_ListRow(r, flags)` and `Th_Card(r, flags)` draw the surface only and return its opaque fill; the caller draws the content.
- Card: `TH_PANEL`, `TH_LINE` border; hover `TH_HOVER_BORDER`; selected accent border on `TH_SELECTED_FILL`; `TH_LR_WITHIN` a faint accent border.
- Row: no border; hover a `TH_HOVER` plate; selected `TH_SELECTED_FILL` with a 3 px accent bar at the left.
- The caller passes the state (`TH_LR_HOVER`, `TH_LR_SELECTED`, `TH_LR_WITHIN`, `TH_LR_DISABLED`), because a scrolled list computes its own hover.
- Used by the Typefaces boxes (a card; the header and variants are rows), the Fonts cards, the Typeface view's font cards, the Glyphs view's cells, the font view's grid cells, the Layers cards, the Add Layer font list, and the Usage and Similar rows.
- Script names stay: `@card:N`, `@hdr:N`, `@tv.open.N`, `@lay_row_N`, `@lay_font_N`, `@usage_row_N`, `@similar_row_N`, `@fcell:N`.

#### Text that does not fit

`Th_FitText(s, face, maxW, out, cap)` shortens with an ellipsis; `Th_TextClip` draws so; no view has a fitting helper of its own.

#### Toast

- `Th_Toast(kind, fmt, ...)` pushes, `Th_ToastDraw(area, left)` draws; `Th_ToastCount` and `Th_ToastHas` are for scripts.
  Kinds: `TH_TOAST_INFO` (accent bar), `TH_TOAST_OK` (green bar), `TH_TOAST_ERR` (`TH_DANGER` bar, 6 s instead of 3.2 s).
- A command reports through `Th_Toast`; no view keeps a toast queue of its own.
- A raised surface 36 px high with a 3 px coloured bar, one line; up to four stacked from the bottom right of `area` (bottom left with `left`), fading in and out.
- `main.c` keeps toasts clear of the browser's side panel and, in the Typeface view, at the left end.

#### Popover and menu

- `Th_Raised(r, alpha)` is everything that floats: soft shadow, `TH_PANEL` fill, hairline, `TH_RADIUS`.
- `Th_Menu(&(ThMenu){items, n, .x, .y, .w, ...}, &open)` returns the index picked, else -1, and clears `open` on a pick, Escape or a click outside (not on `.keep`).
- Items are `TH_ROW_H` high, hover a `TH_HOVER` plate, the shortcut (`.key`) small and dim at the right.
  Flags: `TH_MI_OFF` (inert, its `why` goes to the status bar), `TH_MI_DANGER`, `TH_MI_SEP`, `TH_MI_CUR` (a dropdown's current choice), `TH_MI_DIM` (a placeholder such as "Any").
- The menu stays in the window; `TH_MENU_RIGHT` makes `.x` its right edge.
- A long list passes `.get`, `.ud`, `.vis` and `.scroll`, and scrolls by the wheel and the scroll bar.
- Up and Down move a highlight (no wrap, disabled items skipped), Home and End jump, Enter picks, Escape closes; the mouse moves the highlight too, and a long list keeps it in view.
  `Th_MenuHi` is the highlighted index.
- `Th_Dropdown` is the closed box of a dropdown.
- An item registers `.name`, else its label (`@file.save`, `@Edit_Glyph`, `@Ctx_Open`).
- Used by the top Menu button, the font view's "..." menu, the context menus of the glyph grid and of the library views, and the Style, Format and Colours dropdowns.

#### Status bar

- `Th_StatusBar(r, reserve, left, nl, right, nr)`: a `TH_ROW_H` strip in small text, parts from the left and from the right; `reserve` is room for a control of the caller.
  A part that does not fit is dropped; `TH_SP_TAIL` cuts a path at its start; a hash part is `Th_HashShort` with `TH_HASH_SMALL`.
- Left parts start 12 px from the edge with 14 px between them; a text part has a colour (default dim) and at most `maxFrac` of the width.
- Users: the application's bar (the file and whether it is modified, the library, font or finder, and at the right the hover hint or the glyph under the mouse) and the glyph view's line (`editor.c`: position, tool, ink, zoom and bitmap at the left, the glyph hashes at the right, the Checker toggle in the reserved end).
  The library views have no bar of their own.

#### Scroll bar

- `Th_ScrollBar(view, &scroll, content, step, live)`, and `Th_ScrollRows` for a list of rows; it clamps `scroll` in whole pixels, so a view keeps no clamp of its own.
- An 8 px track inset 4 px at the right (`TH_SB_W`, 12 px, is kept clear); the thumb a pill at least 36 px high, 4 px wide at rest, 8 px and hot when hovered or dragged; no bar while the content fits.
- The wheel scrolls `step` px a notch (0: the view takes the wheel, as the font grid does for Ctrl+wheel zoom); a wheel already used this frame (`Th_WheelUsed`) is not taken again.
  A click on the track centres the thumb there and drags on; one bar is dragged at a time.
- `live` false gives no input; the result is true while the bar is hovered or dragged, so the view must not react under it.
- A menu moves its first row to keep the keyboard highlight in view, and the bar follows (`Th_ScrollRows`).
- A view that needs the scroll before it draws calls the function before its content; the sample editor and the panels call it after the content they draw under the bar.
- Used by the font grid, the library views, the Glyphs view, the side panels, the Usage list, the sample editor and preview, the Add Layer font list, the long menus and the palette bar.
- No view keeps a drag state, thumb calculation or bar drawing of its own.

#### Colour picker

- `src/colorpick.c`: one modal for every place that chooses a colour, drawn with `Th_Raised`, `Th_TextHead`, `Th_Segmented`, `Th_TextField` and `Th_Button`.
- `Th_ColorPickOpen(rgba, title, tag)` opens it on a colour (0xRRGGBBAA) with a non-zero tag naming the question; `Th_ColorPickTake(tag, &rgba)` answers once: 1 and the colour for OK, -1 for Cancel, 0 while none; `Th_ColorPickIsOpen()`; `Th_ColorPickDraw(W, H)`.
  `main.c` draws it last and blocks every other input while it is open.
- Models by a segmented control: HSV (a saturation/value square and a hue bar), Wheel (a hue and saturation disc and a value bar), RGB, HSL and CIE Lab (three gradient sliders each), each with an alpha slider.
- At the right: the new and the old colour over a checker (a click on the old one takes it back), a hex field (`RRGGBB` or `RRGGBBAA`), the colour as RGB, HSV and Lab in small text, Cancel and OK.
- Enter confirms, Esc cancels (also in the hex field); Tab in the hex field takes its value.
- The last model is kept in `[app] pickerMode` (0 HSV, 1 Wheel, 2 RGB, 3 HSL, 4 Lab).
- `src/colormath.c` (no raylib): sRGB gamma, HSV, HSL, CIE Lab (D65, IEC 61966-2-1 matrices; a Lab colour outside sRGB is clamped and reported), packing, hex, relative luminance.

### Colours by role

Every colour is a `TH_*` token in `theme.h`, computed from one (`Fade`, `th_mix`), or data (a glyph pixel, a layer colour); a role two views share is one token.
The light neutral theme: window `#F3F3F4`, panels `#FFFFFF`, hairlines `#DCDCE0`, text `#1E1E22`, secondary text `#7A7A85`, accent `#2F6FED`, accent tint the accent at 15 %.

| Role | Token |
| --- | --- |
| Window, panel, caption strip | `TH_BG`, `TH_PANEL`, `TH_PANEL_ALT` |
| Hairline, faint line, checker square | `TH_LINE`, `TH_LINE_SOFT`, `TH_CHECKER` |
| Segment and progress track, transparency swatch | `TH_TRACK` |
| Text: normal, dim, off, hint, warning, note, error, on accent | `TH_TEXT`, `TH_TEXT_DIM`, `TH_TEXT_OFF`, `TH_TEXT_HINT`, `TH_TEXT_WARN`, `TH_TEXT_NOTE`, `TH_TEXT_ERR`, `TH_TEXT_ON` |
| Interactive and selected, hover, destructive | `TH_ACCENT`, `TH_ACCENT_TINT`, `TH_HOVER`, `TH_DANGER` |
| Content ink (sample text, browser renderings) | `TH_INK` |
| Modal veil, tooltip, read-only notice, selected text | `TH_SCRIM`, `TH_TIP_BG` and `TH_TEXT_TIP`, `TH_NOTICE_BG`, `TH_SELECTION` |
| Text on a light / dark swatch | `TH_ON_LIGHT`, `TH_ON_DARK` |
| Layer palette | `TH_DANGER`, `TH_ACCENT`, `TH_M_CAP`, `TH_M_XHEIGHT`, `TH_M_UNDERLINE`, `TH_LAYER_PINK`, `TH_LAYER_TEAL`, `TH_TEXT` |

- Metric lines, the same everywhere: baseline `#E5484D`, ascent and descent `#8E8E99`, cap height `#2F9E6E`, x-height `#D68A00`, advance `#2F6FED` dashed; underline and strike-out `#9B59D0` dotted.
- The name chip over a glyph box is `TH_PANEL` at 90 %, the baseline in a finder cell `TH_M_BASELINE` at 31 %, the knob shadow black at 9 %.
- The check: `grep -n "(Color){" src/main.c src/browser.c src/editor.c src/finder.c src/layers.c src/insight.c src/sample.c src/panel.inc src/gpanel.inc` finds only zero-alpha `{0}` and colours computed from data.

### Fonts and licences

- Noto Sans v2.015 (`assets/fonts/noto-sans`, `OFL.txt`): SIL Open Font License 1.1; Regular and SemiBold are used.
- GNU Unifont 18.0.01 (`assets/fonts/unifont`: `unifont-18.0.01.otf`, `unifont_upper-18.0.01.otf`, `COPYING`, `OFL-1.1.txt`): SIL OFL 1.1, or the GNU GPL 2 or later with the font embedding exception.

### Markdown rule

This file and `HANDOFF.md` follow the author's Markdown protocol: a heading has one blank line before and after it, paragraphs are separated by one blank line, a list has one blank line before and after it, and every sentence is on a line of its own (also within a list item).

## The font model

- Every position is counted from the baseline and the pen: a glyph's offsets `x` and `y` put the bottom-left corner of its bitmap `x` columns right of the pen and `y` rows above the baseline.
- Advances, offsets and metrics are integers.
- Every font-wide metric is optional, because a pxfm holds only what its source states: `ASCENT`, `DESCENT`, `LINEGAP`, `CAPHEIGHT`, `XHEIGHT`, `INTERNALLEADING`, `QUADWIDTH`, `RATIO`, `UNDERLINEPOSITION`, `UNDERLINETHICKNESS`, `STRIKEOUTPOSITION`, `STRIKEOUTTHICKNESS`, `MONOSPACED`, plus `COLORS`, `SYMBOLIC`, `GLYPH` and `KERNING`.
- A font without `ASCENT` or `DESCENT` is drawn in a box taken from the glyphs; that box is never written.
  Changing `ASCENT` or `DESCENT` moves no glyph, only the box.
- `SYMBOLIC` marks a symbol font, whose codes are not the characters' code points.
- Kerning pairs are edited in the glyph view (see "Kerning"); deleting a glyph drops its pairs, cloning does not copy them.

### Codes

- A code is a group, a number and a variant: `U+000041-000` for U+0041, `X+000003-000` for the fourth glyph without a code point (a symbol), `I+DEFA00-000` for the default glyph; the variants of a glyph are `-001` to `-FFF`.
- In memory and in the pxfdb a code is the 40-bit number `GCCCCCCVVV` (`PxCode`, an `int64_t`; U is 0, X is A, I is F), so U+0041 is `0x41000` and the default glyph `0xFDEFA00000`; numeric order puts the code points first, then the symbols, then the default glyph, each glyph before its variants.
- The file sorts its lines by bytes, so the `I` line comes before the `U` lines there.
- The UI shows `U+0041`, `#3` for a symbol and "Default", with `-001` for a variant (`px_code_label`); a code field takes those, hex with or without `U+` or `0x`, `#n`, `default` and the file form, each with `-V` for a variant (`px_code_parse_user`).
- The symbols are `X+000000-000` up, without gaps; deleting one deletes its variants and renumbers the later symbols and their kerning pairs; a new one takes the next number.
- A variant is a glyph of its own: it is drawn only where it is chosen by its code, a layer or the fit never follows it, and Unifont shows no image for it.
- A text of the version 2 form (six hex digits, offsets `x,y`) is refused as "pxfm version 2 file".

### The default glyph

- The glyph drawn for a missing code is an ordinary glyph with the code `I+DEFA00-000`: its own bitmap, offsets and advance, last in memory, a `GLYPH I+DEFA00-000` line and a `pxfm_glyph` row; nothing else refers to it.
- The glyph count and the uncoded count skip it.
- The grid shows it last, captioned "Default"; it can be opened, edited and deleted, but not given another code.
- "Set Default" copies the selected glyph's bitmap, offsets and advance into it, as an independent copy.

### Hashes and change detection

- Files are named by their hashes; storage is content-addressed, so an edit changes the hash of what it touches, and a glyph's pxgl is never changed in place.
- A font is modified exactly when the canonical hash of its current state differs from the hash the store holds, so an undone edit leaves it unmodified.
- Reader and writer round-trip: every pxfm, pxtf, pxgl and palette, read and written unchanged, gives the bytes and hash of its name; the text writer and the converter from rows agree byte for byte.
- A short hash (8 digits) is shown where space is tight, the full hash in the panels' info blocks; every label names its owner: "Font 3b52125b", "Typeface 8e8c9492", "Glyph c0fc54c2".

### Typefaces

- A pxtf (`pxtf/xx/<hash>.pxtf`) is the fonts of one source file, as variants with text fields (family, style, full name, weight name, slant, copyright, designer, licence…) and the italic, underlined and struck-out flags; `CODEPAGE` is a text key.
- A font that no typeface names is "Orphaned".

### Drafts

The core reads and writes the draft format `PXFEDRAFT 4` (`PALETTE <HASH> <RRGGBBAA,...>` lines, a GLYPH line with the code in the file form, the read palette hash or `-` and the glyph's own palette, `SYMBOLIC`); the UI has no drafts.
A draft of version 2 or 3 (the codes of pxfm version 2) is refused with a message.

## Palettes and colours

### The formats

- A hex palette (`palette/xx/<hash>.hex`, spec `hex.md`): one colour per line, `RRGGBB`, or `RRGGBBAA` when the alpha is not `FF`, upper case; at least one line, no colour twice, no empty line.
- A pxgl of more than one colour is canonical: its first inked pixel in reading order is `0`, the next new colour `1`, and so on; a reader refuses any other numbering.
- A colour font's pxfm has `COLORS n` (n ≥ 2) and the palette's hash as a fifth field on every glyph line with a glyph file; a font of one colour has neither.
  `COLORS` is the number of distinct colours over the first k lines of each glyph's palette, k being the colours the glyph uses.
- A `COLOR` line is refused: "a COLOR line: the colours of a pxfm file before hex palettes (the file was made by an older pixelglyphlib)".
- The pxfdb (format_version 1) has the table `palette` (`hash`, `colors`, `colorset`) with the index `palette_by_colorset`, and the columns `pxfm.symbolic`, `pxfm.colors` (NULL for one colour) and `pxfm_glyph.palette`; the schema in `src/store.c` is copied verbatim from `pxfdb.md`.

### In memory: shared palettes

Each glyph has its own palette, so a font may have any number of colours; a glyph has at most 36 (`PX_GLYPH_MAX_COLORS`, the characters `0`…`Z`), and `.` is transparent.

- `PxFont.pals` is an interned table: no two entries have the same colours and an entry never changes; entry 0 is black alone (`000000`, hash `2f1ab4cf…`).
- A glyph points at an entry (`PxGlyph.pi`) and its pixel characters index it; glyphs of the same colours share an entry, so a font of one colour holds only entry 0 and stays lazy.
- `PxGlyph.pal` keeps the hash of the palette the glyph was read with.
- Each entry carries `draw[37][4]`, laid out as raylib's `Color` and indexed like the pixel characters; `px_glyph_palette(f, g)->draw` is all a renderer needs.
- A palette is changed by copy on write: the new colours are interned and the glyph points at the result.
- `px_font_compact_palettes` drops entries no glyph uses or names and renumbers; it runs when an undo snapshot is taken; `PxFont.palGen` is bumped by every compaction and copy, for caches keyed on indices.

### Canonical order

- `px_glyph_canonicalize(f, g)` renumbers the pixels in reading order and reorders the palette to match: the used colours in reading order, then the unused entries in their old order; a canonical glyph is left alone.
  So slot i is always pixel character i.
- The glyph view canonicalizes the open glyph at the end of every frame without a drag, and compacts the table when it changed something.
- A new colour is appended while the palette has fewer than 36 entries; a palette of 36 gives an unused entry to it; when all 36 are used the colour is refused with the toast "This glyph has 36 colours: drop the colour onto a slot of the palette bar to replace one".
  An inkless glyph on the default palette starts a palette with the new colour alone.
- `px_glyph_add_color(s)` (a 37th colour returns `PX_COLOR_FULL`), `px_glyph_set_colors`, `px_glyph_set_palette`, `px_glyph_used_colors`, `px_glyph_add_slot` (an unused slot), `px_glyph_replace_color` (a slot gets another colour; a colour the glyph has already merges the two slots), `px_glyph_trim_palette` (cut to the used colours).

### The font's colour list

- `px_font_color_list`: the font's distinct colours in order of first appearance (glyph order, then palette order), no length limit; cached in `PxFont.clist` under a key over `palGen` and the glyphs' indices.
- `px_font_is_colour` decides whether the sample text draws the font's colours or the theme's ink.
- `px_font_colors` counts the distinct colours the glyphs use: it is the `COLORS` written, `pxfm.colors` and the Colours filter's count.

### Reading and writing

- Reading (`px_load_glyphs` for files, `load_font_nolock` for the store) reads each named palette once, strictly; `px_font_adopt_palettes` points each glyph at its entry; `px_glyph_from_canonical` checks the glyph is canonical and its palette long enough, with no remapping; `px_font_finish_palette` checks `COLORS`.
- Fonts of one colour load lazily; a colour font is read whole when its first glyph is needed; a font only parsed writes back the `COLORS` it read (`colorsRead`).
- Writing renumbers the glyph text canonically (`px_font_glyph_pxgl`; one colour: every ink pixel `0`).
- A glyph's palette (`glyph_write_palette`): the palette it was read with when its first k lines are still its colours in reading order, so an unchanged font is written back byte for byte; otherwise the used colours in reading order, then the rest of its palette without repeats.
- `COLORS` is written when there are at least two; palettes go to `palette/xx/<hash>.hex` (an existing file is left) and into the table with `OR IGNORE`.
- The colour example of `pxfm.md` is font `894ef821…`, its A `3ad25ae0…`, its palette `ebbba949…`.

### SYMBOLIC

- The Font tab has a Symbolic switch under Monospaced (unlocked fonts, one undo step); library cards show "Symbolic" after "Mono".
- The importers set it: TTF and OTF when the cmap has a (3,0) subtable; FON and FNT for `dfCharSet` 2; BDF and PCF when `CHARSET_REGISTRY-CHARSET_ENCODING` ends in `fontspecific`; PFB for an `Encoding` other than `StandardEncoding`.

## Storage: Load, Save, Import, Export

pxfe works on one database (a `.pxfdb` file) at a time: Load reads it into memory, all work happens in memory, Save writes memory back.
The text folders (`pxtf/`, `pxfm/`, `pxgl/`, `palette/`) are reached only through Import and Export.

### The store

- The library lives in memory as one SQLite database with the pxfdb schema; every view reads it through the store.
- The open font is a `PxFont` document; the store holds what is committed.
- A pxfdb made before hex palettes is refused: "made by an older pixelglyphlib (before hex palettes: colour lines, no palette table): rebuild it from its files with the current pixelglyphlib"; pxfe never migrates it.
- A pxfdb with the codes of pxfm version 2 (more `pxfm_glyph` rows with a variant than without) is refused: "made by an older pixelglyphlib (codes of pxfm version 2): rebuild it from its files with the current pixelglyphlib"; the schema is the same, so only the codes tell.
- `store_glyphs_for_codepoint` (the Glyph usage and the finder) takes every variant of a code point.

### Load, Save, Save As

- Load (`Ctrl+O`) asks for a `*.pxfdb`, checks `meta` (`PXFDB`, version 1) and copies it into memory (`sqlite3_backup`); with unsaved changes it asks first: Save, Discard or Cancel.
- Edits of the open font are committed into the store in place (see "Locks") when its view is left, before Save, Export and Load, and at quit.
- The title bar and status bar show the file and whether memory has unsaved changes: "pixelglyphlib.pxfdb — modified".
- Save (`Ctrl+S`) does `VACUUM INTO` a temporary file next to the database, which then replaces it, so a failure never leaves a broken file; it also writes the settings sidecar.
- Save As (`Ctrl+Shift+S`) writes a new file and continues on it.

### Import (`Menu ▸ Import Folder…`)

- The user picks a folder with `pxtf/`, `pxfm/`, `pxgl/` and `palette/`, then the new database's path (default `<folder>/pxfdb.pxfdb`; an existing file is replaced only after a confirmation).
- Every file is read by the strict readers (with a progress screen); the database is built, written and loaded.
- The build checks that every named palette exists and has a line for each colour of its glyph, that a glyph of several colours has a palette, and `COLORS`.
- A file that fails stops the import with its name and reason; nothing is written.
- Unsaved changes are asked about first; the new database starts with empty settings.

### Export (`Menu ▸ Export to Folder…`)

- A folder without library data gets every file of memory (committed first).
- A folder with library data opens a dialog with the counts found and a choice: Append (write what the folder lacks and leave the files already there, since the same name is the same content; delete nothing), Overwrite (empty its `pxtf/`, `pxfm/`, `pxgl/` and `palette/`, nothing else, then write everything) or Cancel.
- Overwrite offers "Make a backup first", on by default: the folder's data is imported into `<folder>-backup-<YYYYMMDD-HHMM>.pxfdb` beside it first; if that fails, nothing is deleted, and overwriting without a backup needs a second confirmation.
- A summary says what was written, skipped and deleted; the sidecar is not touched.

### Packages (pxfp, pxtp)

A font package (`.pxfp`) carries one font, a typeface package (`.pxtp`) a typeface with all its fonts, from one library to another (pixelglyphlib `spec/pxfp.md`, `spec/pxtp.md`).
Both are plain zip files of library files named by their hashes: the pxfm or pxtf at the root, then flat `pxfm/`, `pxgl/` and `palette/` folders.
`src/package.c` writes, checks and imports them; it uses no raylib.

- Writing takes the text of every file from the rows, as Export does, and gives the bytes pixelglyphlib's `package export` gives: entries in byte order of their names, deflate at level 9, 1980-01-01 00:00:00, Unix mode 0644, no extra field, no comment.
- The deflate is zlib's own, vendored unchanged in `vendor/zlib/` (zlib 1.4, zlib licence), since another deflate (zlib-ng, sdefl) gives other bytes.
- The recommended file name is `<family>-<style>.pxfp` after the first typeface variant that names the font (else `<hash>.pxfp`) and `<family>.pxtp` (else `typeface.pxtp`); a name taken in the folder by other bytes gets `-<first 8 hex digits of the hash>`.
- Reading decides the kind by the file at the root, never by the file name or extension, and checks the whole package before anything is used: the zip itself, the shape of the kind, every name against the hash of the canonical form of its content, that the files name each other exactly, and every file with the strict readers of Import.
- Each file is kept in its canonical form: a package whose files have CR LF line ends or trailing blanks reads as the same files.
- Importing adds the files to memory, all or nothing: a file memory has already with the same text is skipped; one memory has under the same hash with another text stops the import, and nothing is added.
- An import is a change of memory like any other, written by Save; it brings no source file and no font JSON.
- `tests/test_package.c` checks round trips on the test databases, every rejection rule with a crafted zip, and byte identity with packages written by the Python reference (run with zlib's deflate; the Windows builds of Python use zlib-ng).

### Deleting fonts and typefaces

- A font or a whole typeface can be deleted from the library views (`Del`, the context menu, the red buttons of the pinned selection) and from the font view.
- A deleted font's typefaces lose that variant and are rewritten under new hashes; a typeface left with no variant goes.
- Deleting a typeface deletes its pxtf and every font of it that no other typeface names.
- Glyphs and palettes that the deletion leaves unnamed go too; earlier orphans are left alone.
- The confirmation names exactly what goes: "Delete 1 font, 1 typeface (Typeface 8e8c9492 loses variant 0002 and is rewritten), 14 glyphs, 2 palettes?".
- A deletion is made in memory; Save writes it.

### New Font

Menu, New Font asks for name, style, pixel size, ascent and descent (size = ascent + descent) and creates an empty font with no typeface, not yet in the store and counting as modified; its first commit adds it, unlocked.
The name is only the session's title: an orphaned font has no name in the database.

### The settings sidecar

`<db>-settings` (`fsettings.h`, SQLite) holds program settings by hash beside the database: the font layers, their per-glyph corrections, and the locks.

- Schema version 4: the `layer` table (with `em`, `aspect`, `follow`, `mode`, `threshold`), `correction` keyed by the code of pxfm version 3, and `unlocked(kind, hash)` (0 font, 1 typeface; only unlocked things have a row); older files are upgraded in memory and on disk at the next save.
  The upgrade from 3 turns the correction codes into codes of version 3 (`FFFFFF` the default glyph, `F0000n` the symbol n, any other a code point).
- It is read at Load and written at Save; a settings-only change makes the document unsaved without changing the store's change counter, and Save then writes only the sidecar.
- Save As writes a new sidecar; with no settings at all nothing is written and an old sidecar is deleted.
- When a commit changes a hash, `fs_rename_font` and `fs_rename_typeface` move the layers, corrections and lock.

### pxfe.ini

Beside the exe: `lastPxfdb`, the sample text, `[app] fontTab` (0 Font, 1 Glyph, 2 Similar, 3 Layers), `pixelPerfect`, `filled`, `pickerMode`, `[browser] sidetab` (0 View, 1 Similar, 2 Typeface), the browser's view settings and filters (`colors=`).

## Locks

Every font and every typeface has a lock, locked by default; what is unlocked is edited in place.

- A locked font is a viewer: grid, glyph view, selection, zoom and picking an ink work, but every change refuses ("The font is locked: unlock it first") and the editing controls are dim.
- The padlock (`Th_Lock`) is the only way to change a lock: in the Typeface and Font tabs of the panels and in the glyph view's bar.
  Cards, rows and the Typeface view's heading show an open padlock as a mark while unlocked, and no button.
- Unlocking a font or typeface of the library opens its font quietly as the document; the view stays where it is.
- The typeface lock governs the typeface-wide fields; the font lock governs the font's own fields (style, full name, weight name, slant, italic, underlined, struck-out), although they are stored in its typeface variant.
- A lock change is a settings change: unsaved until Save.
- A locked font may still get layers: they are program settings, and the database is not changed.

### Editing in place

`store_edit_in_place` (in `store.c`) takes the open font (and typeface) and the hashes the store holds:

- The edited font is added under its new hash.
- Every typeface naming the old font is rewritten under its new hash, through the deleting plans (`plan_orphans`, `store_apply_delete`).
- The old font goes, with the glyphs and palettes no font names any more; an unchanged hash needs nothing.
- The result lists the typeface hashes that moved; `main.c` carries them to the sidecar, the open views and the navigation.
- A typeface field edit rewrites the typeface the same way.

### The open font

- The document stays in memory across a commit, so undo and redo keep working; `origHash` and `origContHash` follow each commit.
- A font not in the store yet (New Font, Import Font File) is added at its first commit, unlocked.
- Undo and redo (`Ctrl+Z`, `Ctrl+Y`): a snapshot holds the font with its palette table, the settings of the layer its glyphs follow, and the baseline shift.

## Navigation and views

### The bar

- The top bar has a **Menu** button (New Database, New Font, Load, Save, Save As, Import Folder, Import Font File, Export to Folder, Quit) and a segmented control of three segments.
- Exactly one segment is blue: the view the user is in.
- A long segment ends in an ellipsis (`HeadFit`); every segment's hint says where it goes.

| View | Segment 1 | Segment 2 | Segment 3 | Blue |
| --- | --- | --- | --- | --- |
| Typefaces | Typefaces | Fonts | Glyphs | 1 |
| Typeface | the typeface's name | Fonts | Glyphs | 1 |
| Fonts | Typefaces | Fonts | Glyphs | 2 |
| Font | the typeface's name, or "Orphaned" | the font's style, e.g. "Regular" | Glyphs | 2 |
| Glyph | the typeface's name, or "Orphaned" | the font's style | the glyph's caption, e.g. "A U+0041", "#3" | 3 |
| Glyphs | Typefaces | Fonts | Glyphs | 3 |

The style segment carries the "*" modified marker.

### Transitions

- Typefaces, Fonts and Glyphs reach each other by their segments.
- Opening a box header (double click, `Enter`, Open, the context menu) shows the Typeface view; the box stays selected for the way back; the browser switches view itself.
- The blue typeface-name segment of the Typeface view returns to the list.
- Opening a variant row, a Typeface view row or a Fonts card shows the Font view.
- In the Font and Glyph views the first segment goes to the Typeface view; for an orphaned font it leads to the Typefaces list, and the style segment shows the font's own name.
- In the Glyph view the style segment returns to the Font view.
- Alt+Left goes one level up: Glyph, Font, Typeface, Typefaces; from the Glyphs view it returns where it came from.
  Alt+Right goes back to the font that was open, in the view it was left in.
- Leaving a font view commits its edits.

### The Glyphs view

The glyph finder (`src/finder.c`): every distinct bitmap drawn for one code point in the library.

- The grid takes the whole height: the bitmaps on a common baseline at an integer zoom, each with the number of fonts that use it, sorted by that number, height or width.
- Panel tabs **View | Usage**.
  View: the counts; CHARACTER: the search box (a character, `U+00E9`, `00E9`, `0xE9` or `E9`), `<` and `>`, and the preview (Unifont 32, the code point and Unicode block); VIEW: the zoom stepper (starting at 3x) and Sort by (Count, Height, Width).
  Usage: the glyph usage of the selected cell.
- `Ctrl+G` and `Ctrl+F` show View and focus the search box; Find in Library (`Ctrl+Shift+F`) in the font view opens it on the selected glyph's code point.
- A click on a font of the Usage list, or a double click on a bitmap (its most used font), opens that font's Glyph view on that glyph: the bar reads typeface | style | glyph caption, and Alt+Left goes to the font, then up to the Typeface view.
  The Usage list of the font view's Glyph tab opens the font with the glyph selected instead.

### Samples

- One sample text (`src/sample.h`, kept in `pxfe.ini`) is shared by the library views, the font view and the glyph view; it can have several lines; presets: pangram, alphabet, digits and punctuation, "Hamburgefontsiv".
- A missing character is drawn as the font's default glyph, or a hollow box; kerning is applied.

## Library browser

The library views are Typefaces, Typeface and Fonts (`src/browser.c`).

### Lists

- Typefaces: one box per typeface: the family in the header's first row, format, variant count and the typeface hash (small) in its second, and one row per variant: style, size, glyphs, flags, font hash and sample.
  The hashes stand on the baseline of the text beside them.
  Filters work on variants: a box shows when any variant matches, with only the matching variants.
  A variant row selects that font, the header the typeface.
- Fonts: one card per font: family and style (or the hash), details (format, size `ASCENT + DESCENT`, glyph count, Mono, Symbolic, ratio, hash) and the sample, pixel perfect.
- The lists are virtualized; fonts load lazily on worker threads (the pxfm and the glyphs of the sample's characters, cached, reloaded when the sample changes); a 1,200-font library shows its cards within a second.
- A press selects; a press on the entry that is selected already, or Enter, opens it.
  There is no context menu.
- Coming back from a font (Alt+Left or a segment) finds the list with its scroll, filters and selection as they were.
- Counts are in the side panel ("N of M variants", "N of M", "N fonts") and the status line ("N of M typefaces").

### The side panel

- Tabs **View | Typeface | Font | Similar** in the Typefaces and Typeface views, **View | Font | Similar** in the Fonts view.
- View: Rescan in the heading row; FILTER: the name filter (substring over family, full name, style, designer, hash; `Ctrl+F` focuses it), the Style, Format and Colours dropdowns built from the values present, Clear filters; SAMPLE: the sample and its presets; VIEW: the zoom, the columns (1 to 8, or Auto) and the sort (name, size, format, glyph count).
  In the Typeface view only the sample and the zoom.
- Typeface and Font: see "The Typeface and Font tabs".
- Similar: the similar fonts or typefaces of the selection, else "Select a font to see similar ones".
- Nothing is pinned over the panel: every command is in a tab.

### The Colours filter

- A font's count is its number of ink colours, at least 1: `pxfm.colors` (`StoreEntry.colors`) or `px_font_colors` of a loaded font (`LibInfo.colors`).
- The values are Any and the counts present, in numeric order; the list follows the info as it arrives (a font with no ascent or descent is loaded for it while the dropdown is open or a count is chosen), and a chosen count is dropped only when every font's info is known.
- A font whose info is not known does not match.
- Typefaces: as for Style and Format, a variant matches by its font and a box shows with its matching variants.
- Kept in `[browser] colors=`, cleared by Clear filters.

### The Typeface view

- One large card per font: style, details (format, size, glyph count, mono, italic and so on), "Font xxxxxxxx" and the shared multi-line sample at 2x or more.
- The heading carries the family and "Typeface xxxxxxxx".
- `Enter` and a press on the selected card open its font, `Del` deletes it.
- After a deletion the view follows the rewritten typeface by family name, and returns to the list when it is gone.

### The Typeface and Font tabs

`DrawInfoTab` (`src/panel.inc`) draws both, for the library views (through `Browser_SetInfoTab`) and for the font view's Font tab.

- The padlock and "Locked" or "Unlocked: edits change it in place"; the boxed hash (the current one and the stored one while they differ).
- A row of commands: Export Typeface… and Delete Typeface; Clone Font, Export Font… and Delete Font.
  They are queued (`LibCmd`) and run at the end of the frame (`RunLibCommand`), because they may replace the document or rescan the library.
- Typeface: TYPEFACE (the variant count and format at the right): the typeface-wide fields (family, version, designer, URLs, copyright, trademark, licence, vendor, description, code page); an edit sets the field in every variant.
- Font: FONT (its variant number at the right): style, full name, weight name, slant and the italic, underlined and struck-out switches of its variant.
- Read-only fields can be selected and copied; they are editable only for the open document's font and typeface while unlocked (Enter or a click elsewhere keeps, Escape reverts); the first navigation segment shows "*" while it differs from the database.
- Leaving the view, opening a font, Save, Export, Load and quit commit the edit in place; it has no undo.

### Clone Font

- A new variant of the same typeface, under the next number, naming the same font, its style "<style> copy" (`pxtf_clone_variant`, `CloneFont`).
- It needs the typeface unlocked; the open font's edits are committed first.
- The two variants share the font until one is edited: `CommitEdits` points only the open variant at the new font, and `store_edit_in_place`, given the typeface, keeps the old font for the other variant.

### Similar fonts and typefaces

- The 20 best, each with its score and a sample line; a click opens it.
- Font similarity, 0 to 100: shape (0.6), over the shared code points of a reference set (U+0021–U+007E, U+00C0–U+017F): 1 for the same pxgl hash at the same offsets, else the intersection over union of the ink placed at the pen and baseline, averaged; shared glyphs (0.25), the Jaccard index of the pxgl hash sets; metrics (0.15), the closeness of the heights and of the space's (or mean) advance, and the same monospacing.
- Under 10 shared reference code points a pair scores by shared glyphs and metrics only, and is marked so.
- Typeface similarity: the mean over one typeface's fonts of the best score against the other's.
- One font against the library takes under half a second on a worker thread, with the reference bitmaps cached as bit rows.

## Font view

- Top: the toolbar: Menu, the navigation, Undo, Redo and Save; no command bar and no info bar.
- Center: the glyph grid, as in FontLab's font window: each cell shows the glyph at the zoom inside its font box, the advance wide (at least a minimum cell width), with the projected metric lines and a caption strip; Ctrl+wheel zooms.
- Right: the panel.
- Bottom: the sample strip: the shared sample in the current state of the font at 1x to 8x, with kerning, updated as the font is edited, resized by dragging its top edge and folded by its header; then the status bar (file, glyph count, selection count, the stored hash, the current hash when it differs, the modified marker).
  The same strip is under the glyph view.

### The right panel

Tabs **View | Font | Glyph | Similar | Layers**, remembered; selecting a glyph never changes the tab, and each tab scrolls by itself.

- View: the zoom stepper (1x to 16x, also Ctrl+wheel), Metrics and Captions.
- Font: the Font tab of "The Typeface and Font tabs", then FONT METRICS: every metric as an optional row (a check for "stated" and a number field), `RATIO` as two fields, `MONOSPACED` and `SYMBOLIC` as switches, and the Baseline row; changes show live on all cells.
- Glyph: SELECTION: the selection's facts as text (with the character), "Open in Glyph View", the commands (Select Occurrences and Find in Library; Add Glyph, Clone, Set Default, Delete), the glyph hash; then the glyph usage.
- Similar: the similar fonts and typefaces.
- Layers: the font layers.
- Every section caption folds (`Th_Section`), and the state lasts for the session.

### Selection and commands

- Click selects, Ctrl+click toggles, Shift+click extends, Ctrl+A selects all, Esc clears.
- Select Occurrences (`Ctrl+Shift+A`) adds every glyph of the same bitmap shape as a selected one.
- Delete (`Del`) after a confirmation; Clone (`Ctrl+D`) to a code typed in a dialog (`U+XXXX`, hex, a character, `-V` for a variant); Add Glyph adds an empty glyph with or without a code; Set Default.
- `Enter` or a double click opens the glyph view.
- Every editing command refuses on a locked font.
- There is no context menu.

### The baseline

The Baseline row (under Descent) has up and down buttons (`@Baseline_up`, `@Baseline_down`); the baseline moves one row and everything else stays put on screen.

- Up: the ascent falls by 1 and the descent grows by 1; down is the mirror.
- Every other stated height from the baseline moves with it (cap height, x-height, underline and strike-out positions); thicknesses, line gap, internal leading and quad width stay; unstated stays unstated.
- Every glyph with ink changes its offset y to stay in place; a glyph without ink keeps (0, 0).
- The font layers move their offset y too, and the followed layer moves what it drew with, so nothing is redrawn.
- A step that would make ascent, descent, cap height or x-height negative is refused (dim button, hint says why).
- One step is one undo step; the undo stack keeps the net shift (`baseShift`) beside each snapshot and moves every font layer by the difference on undo and redo.

### Glyph usage

- With one glyph selected (font view, glyph view, Glyphs view): the bitmap in the selected glyph's colours, the pxgl hash, the size, and "In N fonts · M typeface variants": the fonts that hold the bitmap under any code, and the typeface variants that name them, which can be more than the fonts.
- With a code point a second line, "K use it for U+0065", counts the fonts that draw the bitmap for that code; it is the number the Glyphs view shows under the bitmap.
- One row per font and variant (family and style, font hash) with every code the font uses the bitmap for.
- With several glyphs: the fonts that hold all of them.
- A click opens that font with that glyph selected.

## Glyph view

- The canvas: the font box large (Fit, integer zoom, wheel), a 1 px pixel grid, the metric lines with labels, the pen origin and the dashed advance line, which can be dragged.
- The screen mapping is frozen from mouse-down to mouse-up, so a stroke that grows the font box never shifts the view; it is fitted again after the stroke.
- Painting outside the bitmap grows it, up to 4096 cells a side.
- Around the canvas: the tool strip at the left, the palette bar below, the status line (position, tool, ink, zoom, bitmap; the glyph hashes), then the sample strip of the font view (it folds, resizes and zooms the same way).

### The bar

- One row, as tall as the right panel's tabs, so that their hairlines meet: the padlock, the current tool's options (Pixel-perfect; Filled; Set, Add, Sub, And with 8-way; Contiguous with 8-way), MOVE (four arrows: the selection, or without one the bitmap, by 1 cell) and ADVANCE.
- The way back is the navigation bar, Alt+Left or Esc; the neighbours are in the View tab; the layers and kerning have tabs.

### The right panel

Tabs **View | Glyph | Layers | Kerning** (`src/gpanel.inc`); opening the glyph view shows the Glyph tab.

- View: the glyph's caption with the previous and next glyph (`PgUp`, `PgDn`), the zoom with Fit, Metrics and Checker (`Editor_ViewPanel`).
- Glyph: the glyph's fields, Find in Library, the full pxgl hashes of the glyph as opened and as it is now, and the glyph usage.
- Layers: session and font layers in one stack.
- Kerning: the partner, the kerning of the pair and the glyph's pairs (`Editor_KernPanel`, see "Kerning").

### The facts of a glyph

- Code point, character, advance and offsets are fields, dim while the font is locked.
- Advance: 0 or more; offsets: any sign, dim for a glyph without ink; one undo step, steps on one field within 0.8 s merged.
- Code point: hex, with or without `U+` or `0x`, and `-V` for a variant; Character: one character or the word `space` (a variant keeps its number); applied on Enter, Tab or a click elsewhere.
- Refused with an error toast: a code another glyph has, the default glyph's codes, a symbol code, above 10FFFF, no code, and a symbol that has variants; the default glyph's code fields are dim; an uncoded glyph can get a code, and the later symbols are renumbered.
- The glyph moves to its place in the order (`px_font_remove_glyph`, `px_font_add_glyph`); the view and selection stay on it (`Editor_SetGlyphIndex`); kerning pairs follow the new code; undo and redo find it by code.

### The ink

- The ink is an RGBA colour (`Editor.inkColor`) or `.` (`inkErase`), not a place in a list, so it keeps its colour when slots reorder.
- Drawing with a colour the glyph lacks adds it to the glyph's palette; with no room, nothing is drawn and a toast says so (at most once a second).
- When a glyph opens and the ink is not a colour of the font, the ink becomes the font's first colour.
- The status line shows `ink . (transparent)`, `ink #RRGGBB (key 3)` or `ink #RRGGBB (not in this glyph)`.

### Kerning

- While the Kerning tab shows (`Shift+K` shows it, or leaves it for the Glyph tab), a second glyph, the partner, is drawn after the edited one: its pen is at the edited glyph's advance plus the kerning of the pair (edited glyph, partner).
  The partner is drawn in its own colours over the layers, on a faint box as wide as its advance, with a dashed line at its pen labelled "Kerning -1".
- When the tab shows, the partner is the one chosen before while the font has it, else the edited glyph's first pair, else the edited glyph itself.
  The partner stays when the view moves to another glyph.
- The tab: After (previous pair, the partner field, next pair), Kerning (a number box, -999 to 999), a hint, and the edited glyph's pairs with their values in code order; a click on one makes it the partner.
- The field takes one character, the word `space`, or a code as the Code point field takes it (`U+00E9`, `#3`, `default`); a glyph the font has not is refused with a toast.
- The arrows step through the partners the edited glyph has a pair with, in code order, round the end.
- The kerning is changed by the number box (and the wheel over it), by `Ctrl+Left` and `Ctrl+Right` (one cell a press), and by dragging the partner's ink sideways, cell by cell.
  A press on a cell where the partner has ink and the edited glyph has none drags the kerning; the cursor shows it.
- Every change is one undo step (a drag is one); 0 removes the pair.
- Locked, the partner is shown and the kerning cannot change.
- The status line adds `kerning A V -1`.
- The partner fits in the view when it is chosen, unless the zoom was set by hand.
- Tests: `144_kerning`.

## Tools of the glyph view

### The tool strip

- A vertical strip left of the canvas, from the bar to the palette bar; the canvas, its mapping, Fit and the advance handle use the narrower rectangle.
- Square icon buttons (`TH_ROW_H`), 2 px apart, 6 px padding; 40 px wide with one column, 72 px with two.
- Fourteen buttons in four groups: Pencil, Eraser; Line, Curve, Rectangle, Ellipse, Polygon, Contour; Fill, Eyedropper; Marquee, Lasso, Magic Wand, Move.
- `kTool[]` in `editor.c` (name, shortcut, key, icon, group, selection tool) is read by the strip, the shortcuts, the status line and the tooltips; its rows are the tool numbers (Pencil 0, Eraser 1, Line 2, Rectangle 3, Fill 4, Marquee 5, Lasso 6, Magic Wand 7, Move 8, Curve 9, Ellipse 10, Eyedropper 11, Polygon 12, Contour 13).
  `kStrip[]` is the button order, laid out by `draw_strip`; a new tool is a row in `kTool[]`, a place in `kStrip[]`, a `Tool` value, an icon in `theme.c` and a branch in `canvas_input`.
- Buttons fill a column while they fit; a group that does not fit the rest of a column starts the next (a group taller than a column wraps button by button); 8 px between groups.
  At 1110 x 600 there are two columns, at 1110 x 520 and 480 three, at 900 px high one.
- The active tool is lit; the tooltip is "Name  (Key)"; script names `@Pencil`… and `tool.N`.
- The tools are off while the font is locked or the glyph's own layer is hidden; choosing another tool drops floating pixels and finishes a curve or polygon.
- Every shape tool (Line, Curve, Rectangle, Ellipse, Polygon, Contour) and the Fill is clipped to the selection, grows the bitmap, and is refused on a locked font.

### Common rules of the drawing tools

- One stroke or shape is one undo step; the preview shows while the button is down and the glyph is unchanged until the release.
- The right button draws `.`.
- Alt at the press turns the Pencil, Line, Curve, Rectangle, Ellipse, Polygon, Contour or Fill into the eyedropper for that click; with a reference layer selected, Alt+drag moves that layer instead.
- With a selection, drawing is clipped to it (the preview too) and does not change it; pixels floating from an arrow key land first (one undo step).
- Shift constrains, Ctrl draws from the centre (Alt subtracts in the selection tools); modifiers are read live, and the release uses those held then.
- Esc during a drag cancels it, with no undo step.

### Pencil and Eraser

- Freehand; the Eraser draws `.`.
- Shift+click with the Pencil draws a line from the last cell the pencil or line ended on, then continues freehand; with no last cell (a fresh view, after undo or redo) it is a plain click.
- Pixel-perfect (`[app] pixelPerfect`, off by default): when three consecutive cells of a stroke make an L, the middle one goes back to what it held before; the bitmap is cropped after the stroke if it left a margin.
  Shown for the Pencil, Line and Curve, but only the pencil changes: lines have no L corners, and the curve and contour always apply the rule.

### Line

- Drag from the start cell to the end cell.
- Shift snaps to the nearest 15 degrees and then to a clean slope: 0, 15, 30, 45, 60, 75, 90 degrees become 0, 1:4, 1:2, 1:1, 2:1, 4:1, 1:0; the end is cut to whole steps, so a 2:1 line is runs of exactly two cells.
- Ctrl makes the press cell the middle and mirrors the far end through it; Shift and Ctrl combine.
  Ctrl was chosen because Alt subtracts in the selection tools and moves a selected reference layer.

### Curve

A cubic Bezier made with three drags:

1. The first drag sets the end points like a line (Shift and Ctrl work); a click sets nothing.
2. The second drag pulls the first control point to where it ends, the third the second.
3. The curve is drawn at the third release, or on Enter after the first drag (unset control points lie on their ends).

- Esc and Ctrl+Z cancel; another tool, leaving the glyph, the padlock and every commit draw it as it is.
- Until then it is a preview with handles (a square per end, a circle per control point, a line from each end to its control).
- All three drags use the same button; the path is 8-connected, with no gap, no repeated cell and no L corner.

### Rectangle and Ellipse

- The press cell and the release cell are corners of the box.
- Shift makes a square or circle (the longer side, in the drag's direction); Ctrl draws from the centre; both combine.
- A click is one cell; a box one cell wide or high is a line.

### Polygon

- Each click adds a vertex; a preview line follows the mouse from the last vertex, and nothing changes in the glyph until the polygon is closed.
- Enter, a click on the last vertex again (a double click) or on the first vertex closes and draws it; Esc and Ctrl+Z throw it away; Backspace removes the last vertex (while a polygon is being made, Backspace belongs to it); another tool, leaving the glyph, the padlock and `Editor_Flush` close it.
- Shift makes each new edge a clean angle as for the Line.
- The outline is the line raster through the vertices and back; one vertex is a cell, two a line.

### Contour

A freehand drag that is closed back to its start and drawn at the release (a lasso that paints); the outline is pixel-perfect; a click is one cell and a straight drag a line.

### Filled

- `Filled` (`@Filled`, `[app] filled`, `Editor_Filled` and `Editor_SetFilled`) applies to the Rectangle, Ellipse, Polygon and Contour: off the outline, on the outline and the inside.
- A filled polygon or contour uses the even-odd rule, so a self-crossing shape has holes (a pentagram's middle stays empty).

### Fill

- The region is the glyph box as the wand sees it: the advance across the font box (ascent plus descent), widened to the bitmap where that reaches out; empty space can be filled and the bitmap grows to hold it, but a cell outside the region is not part of it.
- `Contiguous` (`@Contiguous`, on by default): the cells joined to the clicked one holding the same character; off, every such cell of the region.
- `8-way` is shared with the wand: off joins by sides, on also by corners.
- The selection is a wall and a limit; a click outside it fills nothing.
- A click on a cell that already holds the ink changes nothing.

### Eyedropper

A click makes the colour under the cursor the ink (`.` outside the bitmap or on `.`, so the Pencil then erases).
pxfe has one ink and no second one for the eraser, so the right button picks as the left one does.
Picking changes neither the glyph nor the undo stack, and a locked font refuses the tool like the others.

### The raster

`src/raster.c` (no raylib, no pxfont):

- `ras_line` is Bresenham, walked from its smaller end and reversed when needed, so A to B and B to A cover the same cells and the path runs from the first end.
- `ras_snap` is the constrained angle, `ras_ends` the centre mode, `ras_is_corner` and `ras_pixel_perfect` the L rule, `ras_bezier` the curve sampled at steps under one cell.
- `ras_box(sx, sy, hx, hy, square, center, box)` normalizes a drag with its modifiers; `ras_rect(out, x0, y0, x1, y1, filled)` is the border or the whole box.
- `ras_ellipse(out, x0, y0, x1, y1, filled)` is the midpoint ellipse in a box: four mirrored quadrants stepped from the middles of the sides with integer error terms, so it is symmetric for even and odd sizes; sorted and joined without duplicates; a narrow ellipse stops early so the ring touches all four sides.
  The L rule is applied to the set (a cell with exactly two neighbours, a straight step each along different axes, is dropped), so the ring stays whole and one cell thick.
  Filled fills each row from its first to its last ring cell, so it has the ring's outline and no holes; a box one cell thick is a line, two cells a solid band.
  Very narrow ellipses (a short side of 3 or 4 with a long side of twice that or more) end in a two-cell cap; from a short side of 5 and a ratio under 3 to 1 every ring cell has exactly two neighbours.
- `ras_flood(out, pix, rx, ry, rw, rh, sx, sy, contiguous, diagonals, clip)`: an explicit stack in 4 or 8 directions, or every cell of the seed's character; the `clip` mask is a wall; cells in row order.
- `ras_polygon(out, v, n, closed, filled)`: `ras_line` between vertices; filled adds cells whose centres are inside by even-odd, from sorted edge crossings, without duplicates.
- In the editor `region_pix` builds the region's character grid once for the wand (`sel_wand_at`) and the Fill (`fill_at`).

### The shortcuts of the glyph view

Tools are Shift+letter; the plain keys pick a slot of the palette bar.

| Key | What it does |
| --- | --- |
| `.`, `0`-`9`, `A`-`Z` | pick that slot of the palette bar as the ink (`.` is transparent) |
| Shift+B, Shift+E | Pencil, Eraser |
| Shift+L, Shift+U | Line, Curve |
| Shift+R, Shift+O | Rectangle, Ellipse |
| Shift+D, Shift+C | Polygon, Contour |
| Shift+F, Shift+I | Fill, Eyedropper |
| Shift+M, Shift+Q, Shift+W, Shift+V | Marquee, Lasso, Magic Wand, Move |
| Alt at the click | drawing tools and Fill: pick the ink under the cursor |
| Right button | draws with `.` |
| Shift (mouse) | Line, Curve ends and Polygon edges: clean angles; Rectangle and Ellipse: square or circle; Pencil: a line from the last cell; selection tools: add |
| Ctrl (mouse) | Line, Curve, Rectangle, Ellipse: from the centre; a selection drag: move a copy |
| Enter | draws the curve or polygon being made; drops floating pixels |
| Esc | cancels a drag, the polygon or curve, the float, the selection, then closes the glyph view |
| Backspace | the polygon's last vertex; otherwise clears the selection |
| Delete | clears the selection |
| Ctrl+A, Ctrl+D, Ctrl+Shift+I | select all, deselect, invert |
| Ctrl+C, Ctrl+X, Ctrl+V | copy, cut, paste |
| Ctrl+Z, Ctrl+Y | undo, redo (a float, curve or polygon being made is thrown away first) |
| Arrow keys | move the selection by 1 cell, Shift+arrow by 8; with no selection Shift+arrow moves the bitmap |
| PgUp, PgDn | previous and next glyph |
| Shift+K | the Kerning tab, or back to the Glyph tab |
| Ctrl+Left, Ctrl+Right | the kerning of the pair shown, by 1 |

## Selection and moving

### The mask

`selection.c` (no raylib) holds a selection as a cell mask: a bounding box with a byte per cell in glyph space (x from the pen, y a row counted down from the baseline); a non-empty mask is tight and any shape.

- `sel_combine`: replace, add, subtract, intersect.
- `sel_invert` inverts within the glyph box across the advance and the bitmap.
- `sel_polygon` fills a closed path by cell centres (even-odd), plus the cells the outline passes through.
- `sel_wand` takes the cells joined to the clicked one with the same character, 4- or 8-connected.
- `SelBitmap`, `selbm_clear`, `selbm_stamp` read, clear and write a character bitmap; `SelFloat` is the lifted pixels with their shape and colours.

### Making a selection

- The mode comes from the bar (Set, Add, Sub, And: `@selmode.0` to `.3`) and, winning over it, the keys at the press: Shift adds, Alt subtracts, both intersect.
- Marquee: drag a rectangle of cells; Shift pressed after the press (not already down at it) makes a square from the start cell towards the pointer.
- Lasso: drag a path; the cells inside and on it are selected.
- Magic Wand: click; `8-way` (shown with the wand) joins diagonals, the default is 4-connected; the region is the font box across the advance and the bitmap, so empty cells form groups too, and a click outside it selects nothing.
- A click without a drag (under 3 pixels) outside the selection deselects in Set mode; a drag within one cell selects that cell.
- The shape being dragged is drawn in the accent; Esc cancels it.
- Marching ants: a moving dashed outline of `TH_TEXT` and `TH_PANEL` dashes over a faint accent tint.
- Ctrl+A selects the glyph box and the bitmap, Ctrl+D deselects, Ctrl+Shift+I inverts.
- The selection stays while the view stays on the glyph and goes with a new `Editor_Open`; it is not an undo step.
- It acts on the glyph's own pixels only, never on reference layers.

### Moving: the float

A move lifts the selected pixels into a float drawn over the glyph, their origin drawn empty (a copy leaves it), and the selection moves with it; the drop is one undo step (`EditorBeforeChange` once), a cancel leaves the glyph as it was.

- Lift: a drag from inside the selection with the marquee or lasso in Set mode without Shift or Alt, any drag with the Move tool, the arrow keys, the MOVE buttons; it happens at the first whole cell of movement, so a click inside lifts nothing.
  In the bar's Add, Sub and And modes a press inside the selection changes the selection instead.
- The Move tool with no selection takes the whole bitmap.
- Ctrl at the press moves a copy (Alt is the subtract modifier, so it is not used for this).
- Whole cells only, relative to the cell where the press began.
- Arrows nudge 1 cell, Shift+arrow 8 (`NUDGE_BIG`); without a selection Shift+arrow moves the bitmap by 1.
- A `.` cell of the float does not overwrite; a move first clears all the cells it came from.
- Drop: Enter, another tool, a press outside the float with the marquee or lasso (it also deselects), leaving the glyph (Alt+Left, previous and next, the padlock, Save, Export, any commit through `Editor_Flush`, navigation).
- Cancel: Esc (the next Esc deselects), Ctrl+Z or Ctrl+Y while pixels float, with no undo step; a drop that changes nothing takes none either.
- A drop may land outside the bitmap and the box: the bitmap grows (`ensure`) and is cropped to its ink (`px_glyph_crop`); beyond 4096 cells it is refused with a toast and cancelled.
- The float and the clipboard carry their colours, mapped into the target palette (`px_glyph_add_colors`, reusing slots); a drop that would pass 36 colours is refused with a toast and cancelled.

### Delete, cut, copy, paste

- Delete and Backspace clear the selected cells to `.` (no undo step when none held ink); a moving float vanishes with its origin cleared, a copied or pasted float is thrown away.
- Ctrl+C copies the selected or floating pixels to the program's clipboard (pixels, cells and colours, for the session, into any glyph and font; not the system clipboard); Ctrl+X copies and deletes.
- Ctrl+V pastes as a float at the original cells, or at the view's top-left when they are not all in view; the tool becomes the marquee if it was not a selection tool.
- A locked font or hidden glyph layer refuses Delete, Cut and Paste with "The font is locked".

## The palette bar and the colour picker

### The palette bar

It lies under the canvas and the tool strip, as wide as the editor area.

- Row 1: the transparent tile `.`, then the glyph's slots in canonical order with their keys `0`-`9`, `A`-`Z` small in the corner (dark or light by luminance, `cm_luminance`), then `+` while the glyph has fewer than 36 colours.
- An unused slot has a dashed frame; it keeps its key, can be picked, and sorts after the used ones.
- The ink has an accent frame; the tooltip gives the hex value, the key and what a double-click does.
- Further rows hold the font's other colours, without keys, up to three rows with a scroll bar; a click makes one the ink, and drawing adds it to the glyph.
- Dragging such a colour onto a slot replaces that slot's colour in this glyph (also in a full glyph), onto `+` adds an unused slot; the tile follows the mouse, the target lights, the tooltip says what the drop does.
- A double-click on a slot opens the colour picker (tag "edit"); OK changes the slot (a colour the glyph has merges the slots).
  `+` opens it (tag "add") on the ink and adds the result as an unused slot.
- Every add, replace and change is one undo step.
- The tile pitch is `(width - 22) / 38` clamped to 18..28 px with a 3 or 4 px gap, so `.`, 36 slots and `+` fit one row from 1110 x 600 up.
- A one-colour font shows `.`, black `0` and `+`.
- A locked font lets the ink be picked but has no `+`, drop, double-click or new colour.
- Script names: `pal:.`, `pal:0` to `pal:Z`, `pal:+`, `pal:#RRGGBB`, `palx:N`.

### The reorder animation

When the slots reorder, the swatches slide to their places in 200 ms with a cubic ease-out, tracked by colour so the ink's frame moves with it; a colour new to row 1 glows briefly in the accent.

### Slot keys

- Plain `.`, `0`-`9` and `A`-`Z` (either case) pick that slot when no text field has focus and no dialog or picker is open; a key without a slot does nothing.
- They are read as typed characters (`GetCharPressed`), so the layout's character counts; a letter typed with Shift never picks a slot.

### The unused-slot rule

- Opening a glyph remembers its palette P0 (not again when the same glyph of the same font reopens, as after undo or redo).
- Leaving the glyph (Alt+Left, previous or next, the padlock) and saving, committing or closing (`Editor_Flush`) first canonicalize it; if its palette is P0 nothing changes; else if P0's first k entries are the colours in use it goes back to P0 (a read or shared palette stays reused); else the palette is cut to the used colours.
- So unused slots live while the glyph is edited, and go when it is left unless they were in the palette it was opened with.

### The colour picker

The glyph view uses the design system's colour picker (see "Design system") for `+` and for a double-click on a slot.

## Font layers

Layers are reference glyphs drawn under or over the edited glyph, for comparing and tracing.
A session layer lives for the session; a font layer belongs to a font, is saved in the sidecar, and offers another font's drawing of the same code point for every glyph.

### Session layers

- The glyph view's Layers tab lists the layers top to bottom; the edited glyph is one, marked "Current glyph".
- With no other layer the tab explains layers and shows a primary "+ Add Layer"; afterwards it is a small button in the header.
- The Add Layer popover opens below the bar, so it never covers the bar's controls; it picks a glyph from this font or any font of the library (a searchable list), by character or code, or "same code as the edited glyph" (it follows previous and next), or a file.
- Each layer has visibility (eye), colour (swatches and hex), opacity (0–100 %), x and y offset in pixels (steppers, and Alt+drag in the canvas), order (up and down, or drag in the list) and remove.
- Hiding the current glyph turns the tools off ("Current glyph hidden").
- Session layers stay across glyphs and fonts.

### Sources

- A font of the database (kind 0): pixels.
- A file (kind 1, the native dialog, filter in `flayers.h`): an outline font (`.ttf .otf .ttc .woff .eot .pfb`, `outline.h`), drawn as a smooth fill, or a bitmap font (`.bdf .pcf .fnt .fon .json`, `.ttf/.otf` strikes, `bmsrc.h`), drawn as pixels.
  The popover shows what the file holds; a face, `.fon` font or strike is chosen with a stepper (`face`); a file with outlines and strikes offers "Outlines / Bitmap strikes" (strikes: `face >= 0x10000`).

### Font layers in the stack

- Font and session layers share one stack; a font layer is tagged FONT and adds Size (a file layer) and a "THIS GLYPH" block (offset X and Y, size, "Hide for this glyph", Reset): the glyph's correction.
- Alt+drag moves a font layer for every glyph; an outline layer moves in quarter pixels, the others in whole pixels.
- The popover's LAYER choice: "This glyph only" (session) or "Font layer" (the glyph is always the edited glyph's code); "This font" as source is always a session layer; a file defaults to a font layer.
- Only the order among font layers is saved; a font's layers go to the bottom of the stack when it opens.
- A missing or unreadable file keeps its layer, which says "file missing" or "cannot read: ..."; a changed file is reloaded silently (checked every two seconds while used).
- The open font's layers (`FlDoc`) are copied into the settings under its current hash on the frame after they change, and at the commit and Save when the hash changes.

### Placement and size

- The source glyph's baseline origin sits on the edited glyph's origin, moved by the layer's offset (pixels of the edited font, y up) plus the glyph's correction.
- An outline layer has an absolute size `em` in pixels per em: a point (X, Y) in font units lies at `x = X * em * aspect / unitsPerEm + dx`, `y = Y * em / unitsPerEm + dy` (`aspect` is the width, 100 % square).
  Size and offset step by a quarter pixel and take any typed value (`Th_NumberF`, a comma counts as a point).
- With `em = 0` the size is relative: `fontPx * scale * dsize` pixels per em, `fontPx` being the edited font's ascent + descent.
- A bitmap layer scales by whole multiples, k = round(scale * dsize) from 1 to 16.
- An outline is rasterised at `size * canvas zoom` into a cached texture (64 entries, 8 million pixels, least recently used first); above 1600 px per em it is not drawn.
- The same numbers draw the layer and the pixels of glyphs that follow it.

### Glyphs that follow a layer

One font layer may have `follow`; it must be an outline file with an absolute size.

- When its size, width, offset, mode or threshold, or a glyph's correction, changes, every glyph that is still what the layer drew is drawn again (`FitTick` in `fitdoc.inc`, once per frame).
- A glyph edited by hand is not what the layer drew, so it stays; nothing is stored: the earlier drawing is remade from a copy of the settings at the last redraw and compared.
- The font box follows while ascent and descent are what the layer gave.
- "Redraw All" redraws every glyph, hand edits included.
- On a locked font the followed layer's placement cannot change (the fields are off; a change from elsewhere is put back with a notice); colour, opacity and visibility stay settings.
- Switching follow on redraws nothing; a relative size becomes absolute at its present value.

### The font view's Layers tab

- It lists the open font's font layers.
- For the followed layer (or the first outline layer): Size (px/em), Width (%), Offset X and Y, the follow switch, Smooth or Pixel font, Threshold, "Optimize for This Size" (smooth), "Find Best Fit" (the import's search again) and "Redraw All".
- It counts the glyphs that follow, are edited by hand, or are not in the file.
- The glyph view's Layers tab edits the same layer's offset and size, and the glyph's correction.

## Importing a font file

Menu, Import Font File makes a font of the database from a font file; the file stays under it as a font layer, and its glyphs follow that layer until edited by hand.
The searches and drawing are in `src/fit.c` (no raylib), the document logic in `src/fitdoc.inc`; the same command takes a picture.

- An outline font (`.ttf .otf .ttc .otc .woff .eot .pfb`) is drawn as pixels, as a pixel font or a smooth font.
- A bitmap font (`.bdf .pcf .fnt .fon .json`, a `.ttf` strike) is copied as it is, without the uncoded glyphs; it is always a pixel font and nothing follows its layer.

### Drawing pixels from an outline

- Pixel font: a pixel is inked when its centre is inside the outline; exact also for pixels drawn smaller, larger, or as dots.
- Smooth: a pixel is inked when the outline covers at least `threshold` of it (38 % or 50 % from the search, 5 to 95 % by hand); a glyph without an x correction moves by a quarter-pixel step (half left to a quarter right) to where its pixels best match its coverage, and the layer is drawn moved as much (`FlLayer_Shift`).
- The advance and the font box are the file's advance, ascender and descender in pixels, rounded.

### Finding the grid of a pixel font

`fit_find_grid` ports `pixelglyphlib/readers/ttf.py` (`find_grid`) with its constants, and agrees with it on the 621 TrueType and OpenType files of the real library.
The searches, in order: dots (the lattice of the centres of same-size dots); exact (the common divisor of all coordinates, at most 256 cells per em); coarser (the coarsest divisor close to every coordinate or redrawing 95 % of the ink); lattice (evenly spaced cells fitted to where ink starts and ends); edges (the commonest edge lengths); glyph columns (columns starting anew in each glyph, stored as x corrections); loose (only the cores of the cells compared).
When nothing passes, the best lattice redrawing at least 80 % of the ink is offered as a guess, and the dialog proposes the smooth mode.
It takes about 20 ms for a typical pixel font, 0.8 s for GNU Unifont.
`outline.c` places a TrueType simple glyph `lsb - xMin` to the right of its coordinates (the first phantom point); a composite glyph stays where its parts put it, as fontTools draws it.

### Finding the size of a smooth font

`fit_find_size` tries 5 to 40 pixels per em in half pixels, each with four vertical quarter-pixel positions and two thresholds, on the letters and digits (or the first 80 inked glyphs).

- A glyph is right at a size when its pixels have the parts and holes of its true shape (counted at 200 pixels per em).
- The cover is `1 - sum |coverage - pixel| / sum max(coverage, pixel)`.
- The base is the smallest size with nearly as many right glyphs as any (at most 3.5 % fewer, or one and a half glyphs for a small sample); among it and up to two pixels above, the best score (right glyphs plus half the cover) wins.
- The position is then refined in sixteenths with seven thresholds (30 to 55 %).

It takes about half a second.
A size set by hand (in the dialog, or "Optimize for This Size") gets its position and threshold searched in about 50 ms.

### The dialog

- Family and Style (editable), the face of a collection or the strike of a bitmap font.
- "This is a pixel font": on when a sure grid was found; it cannot be switched on without a grid; always on for a bitmap font.
- What the search found, and a preview drawn as the import will draw.
- For a smooth font, the size in half pixels and "Best Size".
- "Make a typeface with these names".

Import makes a new document not yet in the store: the font, the typeface (one variant with the file's family, style, full name, version, copyright, designer, licence…; HASH is the hash of the file) and the font layer.
A typeface the strict reader would refuse is cut to family and style, or left out.
It is committed like any document, and Save writes the layer under the committed font's hash.

## Importing a picture of a font

Menu, Import Font File also takes PNG, GIF (first frame), JPEG, BMP and TGA.
The importer works out the background, scale, layout, characters and colours, and the dialog lets each be set by hand; the analysis is `src/sheet.c` (no raylib), the reference glyphs `src/sheetref.c`, the rest `src/fitdoc.inc`.

### Background, ink and scale

- Transparent pixels (over 2 % of the picture) are the background; else the commonest colour is; every other pixel is ink.
- Over 300 colours means noise (a JPEG): the background is the commonest colour counted coarsely, and ink differs from it by more than 60 in a channel.
- A preview draws every font pixel as a block of s x s picture pixels: the scale is the largest s (up to 24) for which nearly every colour change along rows and columns lies on a lattice of pitch s (98.5 %, with noise 86 %); each block becomes one pixel (its middle pixel, or with noise its mean).

### Layout

Tried in this order:

- Marker row: a top or bottom row of single pixels of one colour, each over a column that separates two glyphs (the marker's column belongs to none); ink under the next marker that touches a glyph stays with it; what follows the last glyph is dropped.
- Grid: the width is the smallest pitch for which every stretch of ink between blank columns fits one cell, or a slightly larger one that divides the picture's width exactly; stretches far longer than most do not count; the height likewise from the rows.
  When glyphs fill their cells, the picture's period gives the cell (the smallest divisor at which it repeats; for the height the one closest to the width).
  A grid of mostly blank cells is no grid.
- Rows: rows cut at blank pixel rows, glyphs at blank columns; a much lower band (dots, accents) joins its closest neighbour; a stretch about twice as wide as most is cut where the fewest pixels are inked.
  A noisy picture is always rows.

A sheet holding the same glyphs several times (one font in several colours) is told by exactly repeating cells; one copy is imported, chosen in the dialog.

### Characters

Sheets list characters in runs of ascending codes (ASCII from space or `!`, code page 437's upper half after `~`, capitals then digits, `1` to `9` then `0`), so the codes are read as the best sequence.

- Each glyph is compared with the same character of reference fonts (GNU Unifont and Noto Sans Regular, Bold and Black, about 900 glyphs, made in two seconds): the shape on an 8 x 10 grid after stretching the ink box, and the place in the line (top, bottom, width in line heights), which tells `,` from `'` and `o` from `O`.
- A glyph of several colours is read from all its ink and from single colours, and keeps its best reading.
- Dynamic programming over the cells: continuing the previous glyph's run earns 0.5 (a full match is 1), a higher code a little; a blank cell holds a missing character's place, or none.
- Capitals in the places of small letters are accepted; a code given twice stays with the glyph in a run, the other takes its best free code or none.
- Candidates: ASCII, Latin-1, code page 437's upper half and a few more (euro sign, arrows, French ligatures).

A first character typed in the dialog numbers the glyphs from it, cell by cell, with no recognition.
On the 49 test pictures the layout is found for all, and 97.4 % of the 3843 known characters get the right code.

### Colours and metrics

- The ink colours become each glyph's own palette (`px_glyph_set_colors`), shared by the sheet's glyphs; over 36 colours (a JPEG), the cheapest pair is merged until 36 are left.
- `.` gets no colour; one ink colour gives a font of one colour.
- The baseline is where most glyphs end; ascent and descent are how far the ink reaches.
- In a grid or marker strip the advance is the cell width and the glyph keeps its place; a grid makes a monospaced font.
  In rows the advance is the ink width plus a twelfth of the height.
- A space is added when missing; a glyph without a character is kept uncoded.

### The dialog

Family and Style, the layout (Auto, Grid, Rows, Marker row, with what was found), the grid's cell, the scale, the copy of a multi-copy sheet, the first character, "One colour", the findings in words, and a preview of every glyph in its colours over its character (amber when the match is weak); every change runs the analysis again.

## Input rules

### Two gates

A focused text field takes its keys unless a modal dialog or popover blocks the input.

- `Th_SetInputBlocked` (mouse and keyboard): modal dialogs, the progress screen, the frame after a document opened (`swallowMouse`), open popovers and menus, the colour picker, and in `main.c` while a panel field is edited (the canvas then ignores mouse and keys).
  Every call in `browser.c`, `main.c`, `fitdoc.inc` and `sample.c` is of these kinds.
- `Th_SetMouseBlocked` (the mouse only; `Th_MouseBlocked` reads both gates, `Th_MouseOnly` this one): for widgets outside the visible part of a scrolled region or under a pinned block (the browser panel, the Usage and Similar lists).
- The mouse-only readers (`hov` in `browser.c`, the editor's `hit`, the layers' `hov`, `cx_hover`) read `Th_MouseBlocked`.

### Typing while the mouse is away

A text field with the keyboard keeps taking keys when the mouse leaves its scroll area; a click elsewhere ends the editing; `SampleEdit` takes only the modal and popover block.

### Press and release

`Th_Released(rect, name, hover)` is true only when the press began on the same control (name and rectangle), so a control that moved under the mouse is not fired by a press that began elsewhere.
Buttons, toggles, locks, checks, switches, segmented controls, dropdowns, fold headers, hash copies, palette swatches, the Glyphs view's cells, Usage and Similar rows and the Add Layer lists use it; the stepper acts on the press, a menu item on the release of a press that began on the open menu.

### Menu keys

While a menu is open, Up, Down, Home, End, Enter and Escape are the menu's: `Th_MenuKeys` says so, and `IsKeyPressed` / `IsKeyPressedRepeat` are wrapped in `theme.h` to answer false for them; `Th_Menu` reads the raw functions.

### Application keys

`Ctrl+O` Load, `Ctrl+S` Save, `Ctrl+Shift+S` Save As, `Alt+Left` up, `Alt+Right` back to the open font; the font view's and glyph view's keys are in their sections.

## Not done

### Font model and storage

- Kerning pairs are edited only with the edited glyph first: the glyph view shows no partner before it, and lists only the edited glyph's pairs.
- WOFF2 is not supported, as a layer or as an import.

### Design system

- Menus have no checkmarks and no submenus.

### Font view

- No keyboard shortcut for the baseline.
- A font layer added after a baseline step is moved by an undo of that step too.

### Glyph view tools

- No undo of the selection itself.
- No bar buttons for select all, deselect and invert; a single-cell marquee selection needs a drag of at least 3 pixels.
- No system clipboard.
- No selection by colour range, feathering, rotation or scaling of floating pixels.
- No thick brushes or brush shapes.
- A curve's handles cannot be dragged after the third drag, and a curve has two control points at most.

### Palette bar and colour picker

- No keyboard way to reorder or delete a slot; a slot goes by the unused-slot rule.
- The picker is used by the glyph view only; the Layers panel's layer colour keeps its hex field.

### Importing a font file

- Kerning, cap height and x-height are not imported.
- A family of several files becomes several typefaces, not one with several variants.
- A negative advance is imported as 0.
- Changing the followed layer's face, or its file changing on disk, makes every glyph count as edited by hand until Redraw All.
- The searches run on the UI thread, so the window does not answer while they run (under a second, except for very large fonts).

### Importing a picture of a font

- The picture is not kept as a layer under the font.
- A photograph of lettering comes in at the picture's size and in up to 36 colours, not brought down to a pixel grid.
- Kerning is not guessed.
- Characters in no run (punctuation in the designer's own order) get wrong codes; the preview shows them, and they can be changed in the glyph view.
