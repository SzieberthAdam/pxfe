# pxfe: pixel font editor

pxfe is a desktop browser and editor for the pixel fonts of a pixelglyphlib library: pxtf typefaces, pxfm fonts and pxgl glyph files, as text folders or as one pxfdb SQLite database.
It is written in C99 with raylib 6, raygui 5.0-dev and SQLite 3.54 (`vendor/sqlite3/`, static).
The formats are specified in `C:/Users/jnsGEzD3/Documents/GitHub/pixelglyphlib/spec/`: `pxfm.md`, `pxtf.md` (formerly pxfc), `pxgl.md` and `pxfdb.md`.
The whole UI is in English.

> **Version 3 (2026-11-11).** The pxfc container is now the pxtf typeface, glyphs without a code point have codes from `F00000`, the default glyph's line has the code `FFFFFF`, and the library can be one pxfdb file.
> The section "Version 3: the store, glyph usage, similarity and the glyph finder" at the end overrides the older sections where they differ.

## Build

The toolchain is the w64devkit GCC bundled with raylib in `C:\raylib`.

- `build.bat` builds `pxfe.exe` (GUI subsystem).
- `build.bat debug` builds it with a console and debug info.
- `build.bat test` builds and runs the core unit tests.
- `build.bat e2e` builds the scripted end-to-end test binary; `tests\e2e\run_all.bat` runs the scripts.

All `src/*.c` files are compiled into the app, so a new module is just a new file in `src/`.
`vendor/raygui.h` is included with `RAYGUI_IMPLEMENTATION` defined in exactly one file, `src/main.c`.

## Modules

| File | What it does |
| --- | --- |
| `src/pxfont.h/.c` | font model, SHA-256, canonical form, pxfm v2 and pxgl read and write, drafts; no raylib |
| `src/pxtf.h/.c` | typeface model (formerly pxfc), pxtf read and write; no raylib |
| `src/store.h/.c`, `src/store_int.h` | the SQLite store in memory: load a pxfdb, import a folder, save (VACUUM INTO a temp file, then replace), export to a folder (Append, Overwrite), change counter, rows and model both ways, lists, deletion with cascade, glyph usage; no raylib |
| `src/store_sim.c` | similar fonts and typefaces on the store; no raylib |
| `src/library.h/.c` | the browser index from the store lists (the draft listing is kept in the core, unused), lazy and threaded loading from the store; no raylib |
| `src/theme.h/.c` | colours, fonts, raygui style, small drawing helpers |
| `src/browser.h/.c` | the library browser view |
| `src/main.c` | window, app state, views, toolbar with the File menu and Save, Load / Save / Save As / Import / Export flows with their prompts and dialogs, the commit of edits into the store, the locks, glyph grid, metrics panel, status bar |
| `src/editor.h/.c` | the glyph editing view |
| `src/play.inc` | the scripted input player of the end-to-end tests (`--play`) |

## Command line

`pxfe.exe [<file.pxfdb> | <folder>] [--font <hash>] [--typeface <hash>] [--unlock] [--import-to <file.pxfdb>]`.

- A `.pxfdb` file is loaded.
- A folder starts the import flow: the target file is asked for (or given with `--import-to`), the folder is imported, the new database is loaded.
- `--font` and `--typeface` open that font or typeface of the loaded database; `--unlock` then unlocks the font.
- With no argument, the last database (`lastPxfdb` in `pxfe.ini` beside the exe) is loaded, else an empty state with "Load Database…" and "Import Folder…".

## Visual direction

The bar is the polished look of FontLab 8: quiet, flat, precise.

- A light neutral theme: window background `#F3F3F4`, panels `#FFFFFF`, hairline separators `#DCDCE0`, text `#1E1E22`, secondary text `#7A7A85`, accent (selection) `#2F6FED`, accent tint `#2F6FED` at 15% alpha.
- One UI font family: Noto Sans (regular and semibold) and GNU Unifont for hashes and glyph codes, sizes from the type scale in "Typography"; never scaled bitmap text.
- Metric line colours, used everywhere they are projected: baseline `#E5484D`, ascent and descent (box edges) `#8E8E99`, cap height `#2F9E6E`, x-height `#D68A00`, advance `#2F6FED` dashed; underline and strike-out `#9B59D0` dotted.
- Hairlines are 1 px, corners 4 px, no gradients, no heavy borders, no raygui default grey look.
- Glyph pixels are drawn as exact integer-scaled rectangles (pixel perfect), ink in the font's palette colour on the cell background.

## The font model (pxfm version 2)

- Every position is counted from the baseline and the pen position.
  A glyph's offsets `x,y` put the bottom-left corner of its bitmap: `x` columns right of the pen, `y` rows above the baseline (negative is below).
- Advances, offsets and all metrics are integers.
- Every font-wide metric is optional, because a pxfm holds only what its source states: `ASCENT`, `DESCENT`, `LINEGAP`, `CAPHEIGHT`, `XHEIGHT`, `INTERNALLEADING`, `QUADWIDTH`, `RATIO`, `UNDERLINEPOSITION`, `UNDERLINETHICKNESS`, `STRIKEOUTPOSITION`, `STRIKEOUTTHICKNESS`, `MONOSPACED`, plus `COLOR`, `GLYPH`, `_DEF` and `KERNING`.
- A font without `ASCENT` or `DESCENT` is drawn in a box taken from the glyphs (the highest and the lowest bitmap rows); that box is pxfe's own and is never written.
- Changing `ASCENT` or `DESCENT` no longer moves any glyph: it only changes the box.
- The default glyph is an ordinary glyph with the code `FFFFFF` (version 11): it has its own bitmap, offsets and advance, it is always the last glyph of the font, and nothing else refers to it.
- Kerning pairs are kept and written; they are not edited yet, but deleting a glyph drops its pairs and cloning does not copy them.

## Locks and change detection

The files of a library are named by their hashes.
Every font and every typeface has a lock (version 12), and everything is locked until it is unlocked.

- A locked font opens as a viewer: the grid, the glyph view, selection and zoom all work, but every command that would change it refuses ("The font is locked: unlock it first").
- The padlock button (`Th_Lock`) is the only way to change the lock: in the font view's info bar, in the glyph view's bar, on the library's cards, rows and selection block, in the Typeface view and in the Typeface tab.
- What is unlocked is edited in place: the font is changed in the store when its view is left (and before Save, Export, Load and quit).
  Storage is content-addressed, so the hash changes; everything that names the font follows (see version 12).
- A glyph's pxgl is never changed in place: editing a glyph gives it a new bitmap hash, and the glyph view shows the hash the glyph had when the font was opened and the new one.
- Change detection is by hash, never by "an edit happened".
  The font is modified exactly when the canonical hash of its current state differs from the hash the store holds, so an edit that is undone, or a value set to what it was, leaves it unmodified.
- The reader and the writer must round-trip: every pxfm and pxfc of the library, read and written again unchanged, gives the bytes and the hash of its file name.

## Library browser

The browser is the first view when a library root is open.

- Source toggle: "Containers" lists one entry per pxfc variant (with its metadata: family, style, full name, format, designer…); "Fonts" lists one entry per pxfm file (with the metadata of the first container variant that names it, if any); "Drafts" lists the duplicates in the drafts folder.
- Each entry is a card with the family and style (or the hash when there is no name), small details (format, size in pixels `ASCENT + DESCENT`, glyph count, monospaced, ratio, short hash) and a sample text rendered in the font, pixel perfect.
- A side panel has a segmented control at its top, "View" and "Similar", remembered in `pxfe.ini`.
  "Similar" lists the similar fonts (Fonts tab) or similar typefaces (Typefaces tab) of the selected card, and says "Select a font to see similar ones" when there is none.
  It is disabled in the Drafts tab.
- The "View" tab customises the view and is remembered in `pxfe.ini`:
  - a name filter (substring over family, full name, style, designer, hash);
  - a style filter, a format filter and a "Monospaced only" switch, built from the values present;
  - the sample text (editable, with presets: pangram, alphabet, digits and punctuation, "Hamburgefontsiv");
  - the zoom multiplier (1x to 8x);
  - the number of grid columns (1 to 8, or Auto);
  - the sort order (name, size, format, glyph count).
- Characters a font has no glyph for are drawn as its default glyph, or a hollow box.
- The list is virtualized, and fonts are loaded lazily on worker threads: only the pxfm and the pxgl files of the sample text's characters, cached and loaded again when the sample text changes.
  The scan of a 1,200-font library must show its cards within a second; the samples fill in as they load.
- Clicking a card selects it; double-click or Enter opens the font view on it (read-only); "Duplicate to Edit" opens a duplicate.
- Back (`Alt+Left`, or the "Library" button) returns from the font view to the browser, keeping its scroll, filters and selection.

## Font view

- Top: a toolbar with Library (back to the browser), Duplicate to Edit or Save Draft and Save to Library, Undo and Redo, a zoom selector (1x to 16x), toggles for metric lines and captions, Select Occurrences, Clone, Delete, and a selection summary.
- Center: the glyph grid, as in FontLab's font window.
  Each cell shows the glyph at the chosen integer scale inside its font box, the glyph's advance wide (at least a minimum cell width), with the projected metric lines, and a caption strip: the character (when printable) and `U+XXXX`, `-` for a glyph without a code point, and a "DEF" badge on the default glyph.
- Right: a panel with a segmented control at its top, so that nothing is far below the fold:

  - "Font": every metric of the model as an optional row (a checkbox for "stated", and a spinner), `RATIO` as two spinners, `MONOSPACED` as a switch.
    Every change is projected live on all cells.
  - "Typeface": the text fields of the typeface variant (family, style, full name, weight name, slant, copyright, designer…) and the italic, underlined and struck-out flags, editable in a duplicate.
    It is disabled when the font has no typeface.
  - "Glyph": the selection (code point, advance, bitmap, offsets, pxgl hash), a "Find in Library" link for the selected code point, and the glyph usage.
  - "Similar": the similar fonts and the similar typefaces.

  Selecting a glyph never changes the tab.
  The tab is remembered in `pxfe.ini` (`panelTab`), and opening the glyph view shows the Glyph tab.
  Each tab scrolls by itself.
- Bottom: a status bar with the file, the glyph count, the selection count, the original hash, the current hash when it differs, and the modified marker.

## Selection and commands

- Click selects one glyph, Ctrl+click toggles, Shift+click extends a range, Ctrl+A selects all, Esc clears.
- "Select Occurrences" (`Ctrl+Shift+A`) adds every glyph whose bitmap is the same shape as one already selected.
- Delete (`Del`): deletes the selected glyphs after a confirmation.
- Clone (`Ctrl+D`): clones the selected glyph to a new code point typed in a dialog (`U+XXXX`, hex, or a character).
- Edit (`Enter`, double-click): opens the glyph view; in an original it is read-only.
- Undo and redo (`Ctrl+Z`, `Ctrl+Y`): a snapshot holds the font and, for a container font, the container, so edits of the container's text fields and flags are undone like any other.

## Glyph view

- The glyph's font box large (auto-fit, integer zoom, wheel), a 1 px pixel grid, the metric lines with labels, the pen origin and the dashed advance line.
- Pencil, eraser, line, rectangle and fill; painting outside the bitmap grows it; Shift+arrows move the bitmap (its offsets); the advance is a spinner and a draggable line.
- The screen mapping (origin and zoom) is frozen from mouse-down to mouse-up, so a stroke that grows the font box never shifts the view under the cursor; the view is fitted again after the stroke.
- A palette strip of the font's colours.
- Previous and next glyph (`PgUp`, `PgDn`), `Esc` back to the grid.
- A preview strip with a sample text at 1x and 2x, with kerning applied.

## Version 3: the store, glyph usage, similarity and the glyph finder

### Format changes

- pxfc is renamed pxtf (`pxtf/xx/<hash>.pxtf`, a typeface: the fonts of one source file); `SAMPLETEXT` is gone, `CODEPAGE` is a text key.
- A glyph without a code point has a code from `F00000` up, numbered `F00000`, `F00001`, … without gaps, so deleting such a glyph renumbers the later ones and their kerning pairs, and a new uncoded glyph takes the next code.
- The default glyph's extra line has the code `FFFFFF`; `-` and `_DEF` are gone.
- The UI shows a code point as `U+XXXX` and an uncoded glyph as `#n` (its code minus `F00000`).

### The store

The whole library lives in memory as one SQLite database with the pxfdb schema, whatever form it was opened from.

- Opening a `.pxfdb` file checks `meta` (`PXFDB`, version 1) and copies the database into memory (`sqlite3_backup`).
- Opening a text root lists the names under `pxgl/`, `pxfm/` and `pxtf/`.
  The files are named by their hashes, so two libraries with the same sets of names hold the same files: when `<root>/pxfdb.pxfdb` or pxfe's own cache (`cache\<hash of the root path>.pxfdb` beside `pxfe.exe`) has exactly those sets of hashes, it is loaded; otherwise the database is built in memory from the files on worker threads, with a progress bar, by the strict readers, and then written to pxfe's cache.
  pxfe never writes `pxfdb.pxfdb` into the library.
- Every view reads the library through the store: the browser's entries, the fonts it draws, the glyphs, and the queries below.
- The font being edited is still a `PxFont` working copy; the store holds only what is saved.
- Save to Library writes the new files where the library came from: new pxgl, pxfm and pxtf files into a text root, or new rows into the `.pxfdb` file (no existing row is changed or deleted).
  The new rows go into the in-memory store as well, so the new font shows at once everywhere.
- The writer of the text files and the converter from rows must agree: every pxfm and pxtf made from the store is byte for byte the file of that hash, for the whole library.

### Glyph usage

- When one glyph is selected (in the font view, the glyph view or the glyph finder), the "Glyph" tab of the right panel (the finder's right panel) shows "Glyph usage".
  It shows the pxgl hash, the size, and "In N fonts · M typeface variants": the fonts that hold the bitmap under any code, and the typeface variants (pxtf rows) that name them, which can be more than the fonts.
  When the glyph has a code point (the font view knows the selected glyph's, the finder passes its own), a second line "K use it for U+0065" counts the fonts that draw the bitmap for that code.
  This is the number the finder shows under the bitmap ("K fonts").
  The list has one row per font and typeface variant (typeface family and style, font hash) with every code point the font uses the bitmap for, wrapped over several lines.
- With several glyphs selected it shows the fonts that hold all of them.
- Clicking an entry opens that font (read-only) with that glyph selected.

### Similar fonts and typefaces

- The "Similar" tab of the browser's side panel and of the font view's right panel shows "Similar fonts" for the selected font, and "Similar typefaces" for a typeface.
  They list the 20 best, each with its score and a sample line, and a click opens it.
- Font similarity, from 0 to 100:
  - shape (weight 0.6): over the code points both fonts have from a reference set (U+0021–U+007E and the letters U+00C0–U+017F), per code point 1 for the same pxgl hash at the same offsets, else the intersection over union of the inked pixels, both placed at the pen and the baseline; the mean;
  - shared glyphs (weight 0.25): the Jaccard index of the two sets of pxgl hashes;
  - metrics (weight 0.15): the closeness of the font heights and the advance of the space or the mean advance, and the same monospacing;
  - a pair with fewer than 10 shared reference code points scores by shared glyphs and metrics only, and is marked as such.
- Typeface similarity: the mean over the fonts of one typeface of the best score against the fonts of the other.
- One font against the whole library takes under half a second, on a worker thread, with the reference bitmaps of every font cached as bit rows.

### The glyph finder

- A third top-level view beside Library and the font view: "Glyphs" (`Ctrl+G`).
- A search box takes a character typed from the keyboard, `U+00E9`, `00E9`, `0xE9` or `E9`; it shows the code point, the character large in the UI font, and its Unicode block.
- The results are every distinct bitmap drawn for that code point in the library, as cells on a common baseline at an integer zoom, each with the number of fonts that use it, sorted by that number (or by height or width).
- Selecting a cell shows its glyph usage; double-clicking a font of the list opens it with that glyph selected.
- From the font view, "Find in Library" (`Ctrl+Shift+F`) opens the finder on the selected glyph's code point.

## Version 4: storage tiers, glyph view, layers and samples

This section overrides the older ones where they differ.

### Where the work goes: Memory, PXFDB, PXTF-FM-GL

The user always knows where the work is.
There are three places, named the same everywhere in the UI:

- **Memory**: the in-memory store of the running pxfe.
  It is lost when pxfe quits, unless it was saved on.
- **PXFDB**: the `.pxfdb` file of the library: the file that was opened, or `<root>/pxfdb.pxfdb` for a library opened as a folder (created on the first save when missing).
- **PXTF-FM-GL**: the text folders `pxgl/`, `pxfm/`, `pxtf/` of the library root: the folder that was opened, or the folder of the opened `.pxfdb` (asked once when that folder holds no library folders).

The duplicate being edited is the working copy; it is not in any of the three until it is saved.

- The toolbar has the save pipeline on its right: three buttons from left to right, with right-pointing arrows between them:
  `[Save to Memory] → [Save to PXFDB] → [Save to PXTF-FM-GL]`.
  - Save to Memory (`Ctrl+S`) puts the working copy into Memory as new rows (its pxgl, pxfm and, for a typeface duplicate, a new pxtf); it is enabled while the working copy differs from what Memory holds for it.
  - Save to PXFDB inserts every row that Memory has and the PXFDB file lacks; it is enabled while there are such rows.
  - Save to PXTF-FM-GL writes every file that the PXFDB has and the folders lack; it is enabled while there are such files.
  - Each button shows a badge with what it would save ("2 fonts · 1 typeface · 14 glyphs") and its target path in the tooltip; a disabled button says why ("Nothing new in Memory").
  - Nothing is ever overwritten; files and rows are deleted only by a deletion the user made (see "Deleting fonts and typefaces").
- The status bar names the tier state in words: "Working copy: modified · Memory: 2 fonts not in PXFDB · PXFDB: up to date with the PXTF-FM-GL".
- Quitting or opening another library with work in the working copy or in Memory that is not in the PXFDB asks first.
- Opening a library folder: when `<root>/pxfdb.pxfdb` exists, it is loaded into Memory; the files of the folders that it lacks are read and added to Memory (so Memory is ahead of the PXFDB by them), and the rows it has that the folders lack count as pending for the PXTF-FM-GL.
  When there is no `pxfdb.pxfdb`, Memory is built from the folders (with the progress screen) and everything is pending for the PXFDB.
  pxfe's own cache folder is no longer used.
- Opening a `.pxfdb` file loads it into Memory; the PXTF-FM-GL is its folder when that folder has library folders.
- Drafts stay for unfinished working copies across sessions: "Save Draft" (`Ctrl+Shift+D`) writes `drafts\<name>.pxfd` beside `pxfe.exe`, and the toast, the status bar and the Drafts tab show the full path, with an "Open Folder" button.

### Hashes

- A short hash (8 digits) is shown where space is tight: cards, lists, captions, info bars.
- The full hash is shown in the right panel's info blocks (font, typeface, glyph), selectable for copying (click copies, with a toast).
- Every label says what the hash belongs to: "Font 0820dc69", "Typeface b925951b", "Glyph c0fc54c2".

### Glyph view

- In the glyph view the font view's second toolbar row (zoom, metrics, captions, selection commands) and the "Original — read-only" bar are hidden; the canvas gets the space.
- The glyph view has its own compact bar: Back, previous and next glyph, the glyph's name, tools, palette, zoom (fit, minus, value, plus), metric lines on and off, the move and advance controls.
  An original shows a small "Read-only · Duplicate to Edit" chip in this bar instead of the big band.
- Its right panel has three tabs: Glyph (code, advance, bitmap size, offsets, full pxgl hash of the original and the current bitmap), Usage (the glyph usage list) and Layers.
- The way in is shown in the font view: its right panel's Glyph tab has an "Open in Glyph View" button (also `Enter`, or a double click on the glyph) for the selected glyph.
- The bar has a "Layers" toggle after "Metrics"; it reads "Layers 2" when two reference layers exist, it is lit while the Layers tab shows, and a click selects that tab.

### Layers

- Layers are reference glyphs drawn under or over the edited glyph in the glyph view, for comparing and tracing; they are not saved into the font.
- With only the current glyph the Layers tab shows an empty state: what layers are for and a prominent blue "+ Add Layer" button (once a layer exists the button is the small one in the tab's header).
- The Add Layer popover opens below the glyph view's bar, so it never covers the bar's controls.
- The Layers tab lists the layers from top to bottom; the edited glyph is one of them, marked as "Current glyph" and highlighted.
- "Add Layer" picks a glyph: from this font or from any font of the library (a searchable font list), by character or code (`A`, `U+0041`), or "same code as the edited glyph" so that it follows previous and next.
- Each layer has: visibility (eye), colour (swatches and hex), opacity (0–100 %), x and y offset in pixels (spinners, and dragging with Alt in the canvas), order (up and down, or drag in the list), and remove.
- The current glyph layer can be hidden too; while it is hidden the editing tools are disabled and the canvas says "Current glyph hidden".
- The layers stay while moving to the previous or next glyph and while switching fonts, for the session.

### Samples

- The sample text is one shared setting (`pxfe.ini`) for the browser, the font view and the glyph view, and it can have several lines (Enter makes a new line in the sample box).
- The font view has a sample strip at the bottom, as the glyph view has: the sample in the current working copy at an integer zoom (1x to 8x), with kerning, updated as the font is edited; the strip can be resized by dragging its top edge and collapsed.
- Missing characters show the default glyph or a hollow box, as in the browser.

### Library: typefaces

- In the Typefaces tab one typeface is one box: its family name, the typeface hash (short), format and number of variants in the header, and inside it one row per variant: style, size, glyphs, flags, the font hash (short) and the sample.
- Filters work on variants; a typeface shows when any variant matches, with the non-matching variants hidden.
- Clicking a variant row selects that font; the box header selects the typeface (for similar typefaces).
- The Fonts tab stays one card per font.
- The badge of the Typefaces tab counts typefaces (boxes), not variants; the panel and the status line say "N of M variants" and, in the status line, "N of M typefaces".

### Deleting fonts and typefaces

- A font (pxfm) or a whole typeface (pxtf) can be deleted: from the browser (a card, a typeface box or one of its variant rows, `Del` or the context menu) and from the font view.
- The browser's side panel shows the selection pinned at the bottom with red buttons under Open and Duplicate to Edit: "Delete Font" (a card or a variant row) and "Delete Typeface" (a box header or a variant row); `Del` and the context menu do the same.
  The toasts of the browser keep clear of the side panel, so they do not cover these buttons.
- Deleting a font removes its pxfm; every typeface that names it loses that variant, which gives that typeface a new pxtf (the old one is deleted), and a typeface left with no variant is deleted.
- Deleting a typeface deletes its pxtf and every font of it that no other typeface names.
- The glyphs (pxgl) that the deletion leaves named by no pxfm are deleted too; glyphs that were orphans before are left alone.
- A confirmation names exactly what goes: "Delete 1 font, 1 typeface (Typeface b925951b loses variant 0002 and is rewritten), 14 glyphs?".
- A deletion is made in Memory first, like any edit, and travels with the pipeline: Save to PXFDB deletes those rows from the PXFDB, Save to PXTF-FM-GL deletes those files from the folders; the badges count deletions apart ("2 fonts · −1 font").
  Before a save that deletes, a confirmation lists what will be deleted there.

## Version 5: Load, Save, Import, Export

This section replaces "Where the work goes: Memory, PXFDB, PXTF-FM-GL" of version 4, and the drafts, entirely; it overrides every older section where they differ.

### The model

- pxfe works on one database (a `.pxfdb` file) at a time.
- **Load** reads the database file into memory; all work happens in memory; **Save** writes memory back to the database file.
- The text folders (`pxtf/`, `pxfm/`, `pxgl/`) are reached only through **Import** and **Export**.
- There are no tiers, no save pipeline and no drafts any more; the Drafts tab, the `.pxfd` files and the storage bar are removed.

### Load (`Ctrl+O`)

- The file dialog looks for `*.pxfdb` files.
- When memory has unsaved changes, pxfe asks first: Save, Discard or Cancel.
- The command line takes a `.pxfdb` file the same way; `pxfe.ini` remembers the last one and loads it at start.

### Working in memory

- Everything is edited in memory: duplicates, glyph edits, metrics, typeface fields, deletions.
- A duplicate being edited is committed into memory as a font (and, for a typeface duplicate, a typeface) when the user leaves its font view, before Save, before Export, and before Load or quit.
  A later commit of the same duplicate replaces its earlier commit in memory (that earlier font, the typeface rewrite and the glyphs only it named go), so editing never leaves a trail of in-between fonts.
- The originals stay read-only as before; deleting a font or a typeface works as in version 4, in memory.
- The title bar and the status bar show the database file and whether memory has unsaved changes: "pixelglyphlib.pxfdb — modified".

### Save (`Ctrl+S`) and Save As (`Ctrl+Shift+S`)

- Save writes memory to the loaded database file, safely: to a temporary file next to it first, then it replaces the old file, so a failure never leaves a broken database.
- Save As writes to a new file and continues on it.
- After Import the database file is already chosen, so Save writes there.

### Import (`File ▸ Import Folder…`)

- The user picks a folder that holds `pxtf/`, `pxfm/` and `pxgl/`, and then the path of the new database file (default: `<folder>/pxfdb.pxfdb`; an existing file is replaced only after a confirmation).
- pxfe reads every file with the strict readers (with the progress screen), builds the database, writes it to that path, and loads it at once.
- A file that fails the strict check stops the import with its name and the reason; nothing is written.
- Unsaved changes in memory are asked about first.

### Export (`File ▸ Export to Folder…`)

- The user picks a folder.
- An empty folder, or one with no `pxtf/`, `pxfm/` or `pxgl/` data, gets every file of memory (committed first) at its library path.
- A folder that already holds typeface, font or glyph data opens a dialog with the counts found there and a choice:
  - **Append**: writes the files of memory that the folder lacks; files that are there already are left (the same name is the same content); nothing is deleted.
  - **Overwrite**: deletes the contents of the folder's `pxtf/`, `pxfm/` and `pxgl/` folders (and nothing else of the folder: other files and folders stay), then writes every file of memory.
    The dialog has the option "Make a backup first", on by default: before anything is deleted, the folder's current `pxtf/`, `pxfm/` and `pxgl/` data is imported into a backup database file, `<folder>-backup-<YYYYMMDD-HHMM>.pxfdb` beside the folder (the path can be changed).
    If the backup cannot be made (a file there fails the strict check), nothing is deleted, and the user can cancel or overwrite without a backup after a second confirmation.
  - **Cancel**.
- At the end a summary says what was written, skipped and deleted.

## Version 6: navigation

This section replaces the top-level navigation "Library | Glyphs | <font>" of the older sections and the Typefaces and Fonts tab row of the browser.
Everything else (the font and glyph views, the library boxes, the file handling of version 5) stays as it was.

### The bar

- The top bar has a **Menu** button (formerly "File"; the same menu: Load, Save, Save As, Import Folder, Export to Folder, Quit) and, after it, a segmented control of three segments.
- Exactly one segment is blue: the view the user is in.
- A segment that is too long is cut with an ellipsis (`HeadFit`); every segment has a hover hint that says where a click goes.

### The six views

| View | Segment 1 | Segment 2 | Segment 3 | Blue |
| --- | --- | --- | --- | --- |
| Typefaces | Typefaces | Fonts | Glyphs | 1 |
| Typeface | the typeface's name | Fonts | Glyphs | 1 |
| Fonts | Typefaces | Fonts | Glyphs | 2 |
| Font | the typeface's name, or "Orphaned" | the font's style, e.g. "Regular" | Glyphs | 2 |
| Glyph | the typeface's name, or "Orphaned" | the font's style | the glyph's caption, e.g. "A U+0041", "#3" | 3 |
| Glyphs | Typefaces | Fonts | Glyphs | 3 |

- **Typefaces**: the browser's typeface boxes, one per typeface.
- **Typeface**: the fonts of one typeface in more detail than the box (see below).
- **Fonts**: the flat list of every font, without grouping.
- **Font**: the font view with its toolbar (zoom, metrics, captions and so on).
  The style segment carries the "*" modified marker.
- **Glyph**: the glyph editor.
- **Glyphs**: the glyph finder.

### Transitions

- Typefaces, Fonts and Glyphs reach each other by clicking their segments; "Typefaces" from the Typeface, Fonts or Glyphs view shows the list of typefaces.
- Opening a box header (double click, `Enter`, the panel's Open, the context menu) shows the Typeface view; the browser keeps the box selected so that the list comes back as it was.
- Clicking the blue typeface-name segment of the Typeface view returns to the Typefaces list.
- Opening a font from the Typefaces view (a variant row), the Typeface view or the Fonts view shows the Font view.
- In the Font view, the first segment goes to the Typeface view of the font's typeface; "Glyphs" goes to the Glyphs view.
- A font that belongs to no typeface reads "Orphaned" in the first segment, and that segment leads to the Typefaces list, since there is no Typeface view for it.
  The style segment of such a font shows the font's own name.
- Opening a glyph in the Font view shows the Glyph view; the style segment returns to the Font view, the typeface segment to the Typeface view.
- Alt+Left (and the Back button of the glyph bar) goes one level up: Glyph, Font, Typeface, Typefaces.
  In the Glyphs view it returns to the view it was entered from.
  Alt+Right goes back to the font that was open (in the view it was left in).
- Leaving a font view commits a duplicate into memory as before (version 5).
- The library views have no segment for the open font any more; Alt+Right is the way back to it.

### The Typeface view

- One large row per font of the typeface: the style, the details (format, size, glyph count, mono, italic and so on), the font hash (short), the Open, Duplicate to Edit and Delete Font buttons, and a multi-line sample (the shared sample of `src/sample.h`, at least 2x).
- The side panel shows the typeface: its name, the full typeface hash, and the fields of its pxtf (designer, license, license URL, copyright, version, vendor, trademark, description, URLs, code page, format); the sample box, the zoom and the Similar tab are those of the library; the filters are not shown.
- The selected font is pinned at the bottom of the panel with its Open, Duplicate to Edit, Delete Font and Delete Typeface buttons; `Enter` and a double click open the font, `Del` deletes it.
- After a font is deleted the typeface is rewritten under a new hash; the view follows it by family name, and returns to the list when the typeface is gone.

### The browser

- The browser's own Typefaces and Fonts tab row is gone; the bar replaces it.
- The counts are in the side panel ("N of M variants", "N of M", "N fonts" in the Typeface view) and in the status line.
- The Rescan button moved to the heading row of the panel's View section.
- The browser does not return the opening of a typeface to the application: it switches view itself (`BROWSER_OPEN_TYPEFACE` is only recorded as the last action).

## Version 7: views

This section changes the side panels, the zoom and the start view of version 6.
The rule of the whole round: the component of a role is reused, never drawn again in another look.
Where a view had its own widget for a role that exists elsewhere, that code is gone.

### The start view

- After a database is loaded at start (or by Load), the program shows the Typefaces view, whatever tab or font the last session ended in.

### One zoom control

- Everywhere a zoom is chosen it is `Th_Stepper` (`src/theme.c`): `[-] 3x [+]`, 96 x 28 px, the control of the font view's toolbar.
- It is used by the font view's toolbar, the glyph view's bar, the sample strip's header, the View tab of the Typefaces, Typeface and Fonts views, and the View tab of the Glyphs view.
- The 1 to 8 segment rows of the browser and the sample strip, and the nine toggles of the Glyphs view (with "Auto"), are gone.
  "Auto" does not fit a stepper, so the Glyphs view starts at 3x.
- `Th_Section` (small-caps section title with a hairline) is shared by the browser's panel and the Glyphs view's panel.

### The Glyphs view

- The top strip (search box, Previous / Next, the character preview with its code point and block) and the toolbar (zoom, sort) are gone; the grid has the whole height.
- The side panel has the tabs **View | Usage**, drawn like the library's (`Th_Segmented`).
- **View**: the heading with the counts, CHARACTER (search box, `<` and `>`, the preview), VIEW (the zoom stepper, "Sort by" as a segmented control: Count, Height, Width).
- **Usage**: the glyph usage list of the shared Insight, as before.
- A click on a font of the usage list (and a double click on a bitmap, which opens its most used font) opens that font's Glyph view on that glyph: the bar reads typeface | style | glyph caption.
  Alt+Left goes to the font, then up to the Typeface view.
  The Usage list of the font view's Glyph tab keeps opening the font with the glyph selected.
- Ctrl+G and Ctrl+F switch the panel to View and focus the search box.

### The Typeface tab

- The font view's right panel is **Font | Glyph | Similar**; its Typeface tab is gone.
- The side panels of the Typefaces and Typeface views are **View | Typeface | Similar**; the Fonts view stays View | Similar.
- The Typeface tab is the content of the former font view tab (`DrawTypefaceTab` in `src/panel.inc`, which the browser calls through `Browser_SetTypefaceTab`): the variant line, the boxed full hash field of the Font tab (click copies it), the fields and the three switches.
  It shows the typeface of the selected box or row, and the variant of the selected row.
- The tab is read-only while the typeface is locked.
- The typeface's lock (a padlock at the top of the tab, and in the Typeface view's heading) unlocks its fields.
  The font of the selected variant is opened for it, and the view stays on the typeface.
- While it is unlocked the fields and the three switches of the variant of the selected row are editable (Enter or a click elsewhere keeps a field, Escape puts its value back), the boxed hash shows the hash in the database.
- The first nav segment shows the family with the "*" marker while it differs from the database.
- Leaving the Typeface view (or any other view), opening a font, Save, Export, Load and quit commit the typeface edit in place: the typeface is rewritten under its new pxtf hash.
- The edit has no undo (the shortcuts are those of the font view).
- The invented panel of the Typeface view (name, a two-line plain hash, the fields as plain text) is gone: the View tab of that view is the sample and the zoom, like the Typefaces view's.
- The hash of a font in the rows of the Typeface view is the "Font xxxxxxxx" label of the other library cards; the heading carries "Typeface xxxxxxxx" like a box header.

### Settings

- `[app] fontTab` (0 Font, 1 Glyph, 2 Similar) replaces `panelTab`.
- `[browser] sidetab`: 0 View, 1 Similar, 2 Typeface.

## Version 8: design system

The user's complaints: the text is too small to read, and views invent their own looks for the same thing.
The rule of this round: one component, one function.
Every use site of a role calls the function of its owner in `src/theme.c`; a view never draws a second variant.
The measure of success is that the user interface code shrinks.
No view code holds a literal font size or colour: both come from `theme.h`.

### Typography

Two faces and one pixel face: Noto Sans Regular, Noto Sans SemiBold, GNU Unifont.
The user rejected system fonts for a font program: the fonts are in `assets/fonts` and are loaded from there.
Exactly three UI sizes (18, 16, 13) and the pixel face at 16:

| Role | Face | Size | Colour | Function |
| --- | --- | --- | --- | --- |
| Heading (view title, dialog title) | Noto Sans SemiBold | 18 | `TH_TEXT` | `Th_TextHead` |
| Body: label, value, button, tab, row text | Noto Sans Regular | 16 | `TH_TEXT` (label: `TH_TEXT_DIM`) | `Th_Text` |
| Caption (section title) | Noto Sans SemiBold | 13 | `TH_TEXT_DIM`, upper case | `Th_Section` |
| Small: status line, hint, footnote | Noto Sans Regular | 13 | `TH_TEXT_DIM` | `Th_TextSmall` |
| Hashes, code points, glyph captions | GNU Unifont | 16 (32 for a large preview) | `TH_TEXT` | `Th_TextUni`, `Th_CodeDraw` |

- The sizes are `TH_SIZE_HEAD`, `TH_SIZE_BODY`, `TH_SIZE_CAP`, `TH_SIZE_SMALL` in `theme.h`; the Noto fonts are fields of `theme`.
- A size in the table is the size of the em, as in CSS: `theme.c` loads Noto Sans at `em * 1.362` (`TH_NOTO_RATIO`, ascender 1069 plus descender 293 over 1000) because raylib's `LoadFontEx` size is the ascent-to-descent height.
- Fonts are found under `assets/fonts` next to the executable (`GetApplicationDirectory()`), else two folders up (the test executable in `tests\tmp`), else the working directory.
  A missing file is named in `theme.missing`, `main` raises an error toast, and raylib's default font stands in (Unifont: the small face); nothing crashes.
- GNU Unifont is a 16 px pixel font and is drawn only at exactly 16 px, or at 32 px (the same pixels at scale 2), with point filtering and whole-pixel positions.
  raylib 6 (stb_truetype) reads the CFF outlines of the `.otf` files; at 16 px every coverage value is 0 or 255 (checked), so no atlas had to be generated by hand.
  The 57 000 glyphs of the BMP are never loaded at start: both files stay in memory and a glyph is rasterised with `LoadFontData` when first drawn, into a 1024 x 1024 point-filtered atlas of 32 x 32 slots.
  A full atlas (1024 glyphs) is flushed and refilled: an evicted glyph is rasterised again when it is needed again.
  Code points of the planes 1 to 16 come from `unifont_upper`.
- Glyph captions (`Th_CodeDraw`, `Th_TextRich`): by default the character itself in Unifont, centred; while the mouse is over the caption or its cell, the text "U+XXXX" in Unifont, centred.
  A glyph without a code point shows "#n".
  A code point with no visible Unifont image shows "U+XXXX" always: controls, space and the other blanks, marks without an advance, and the code points Unifont lacks.
  Sites: the font view's cell captions, the glyph segment of the navigation bar, the Glyph view's bar, the layer rows, the rows of the Glyph usage list, the code beside Find in Library, the Add Glyph dialog; the large preview of the Glyphs view is Unifont at 32 px.
- Rows, buttons and fields are `TH_ROW_H` (28) high, the panel padding is `TH_PAD`, and a button's width is its text measured plus `2 * TH_PAD`.
- The side panel is 352 px wide and the minimum window is 1110 x 600, so the glyph editor's toolbar keeps its width.
- A glyph preview (the large character of the Glyphs view) is Unifont at 32 px, never another size.
- Roles differ by size, weight and colour, never by a size of their own: a new role takes one of the five rows above.
- The wide controls of the library View panel (Columns, Sort by) take a line of their own below their label (`wide_row`).
- Colours by role: text `TH_TEXT`, secondary text `TH_TEXT_DIM`, interactive and selected `TH_ACCENT` with `TH_ACCENT_TINT`, destructive `TH_DANGER`, lines `TH_LINE` and `TH_LINE_SOFT`, surfaces `TH_BG`, `TH_PANEL`, `TH_PANEL_ALT`.
  Hover is `TH_HOVER`.
  No other colour is written in a view.
- Acceptance: at 1400 x 900 and at the minimum window size no text is clipped, overlaps another or is cut off without an ellipsis.

### Spacing

- The grid is 4 px: paddings 4, 8, 12; the gap between controls is 8.
- Control height is 28 (button, toggle, segmented control, text field, stepper); a list row is at least 28.
- A panel has 12 px padding; a section caption takes 24 px of height.
- Radius `TH_RADIUS`, borders 1 px, always through `Th_RectLines`.

### Component catalogue

Each component has one owner function in `src/theme.c` (declared in `theme.h`).
A use site that draws the look itself, or that calls the function with a look of its own, is a defect.
States are normal, hover, active (pressed or selected), disabled and focused where the component takes input.

#### Stepper

- Owner: `Th_Stepper(r, value, prefix, flags)`; `Th_StepperValue(r)` is its middle.
- Look: `[-] 3x [+]`, `TH_ROW_H` high, one line, the value centred in body text; `TH_STEP_W` (96) wide for a zoom.
- States of each button: normal, hover (`TH_HOVER`), pressed (`TH_PRESSED_FILL`), disabled (dim, `TH_STEP_NO_MINUS` and `TH_STEP_NO_PLUS`, set at the limits).
- `TH_STEP_EDIT` is the number field that is being typed into: accent border, the caller draws the text in the middle.
- A pressed button steps once, and a held button repeats after 0.45 s.
- Used by the zoom of the font toolbar, the glyph bar, the sample strip and the library views, the advance of the glyph bar, the metric number fields and the layer offsets.
- Registers `<prefix>-` and `<prefix>+`.

#### Hash display

Exactly two forms, both in Unifont:

- Owner of the short form: `Th_HashShort(kind, hash, x, y, flags, copyName)`.
  It draws a kind word in `TH_TEXT_DIM` (body size; `Font`, `Typeface`, `Glyph`, or none) and the first 8 characters of the hash in Unifont, `TH_TEXT` (`TH_HASH_ACCENT`: `TH_ACCENT`, a changed state).
  `TH_HASH_RIGHT` ends the label at `x`; `TH_HASH_MEASURE` only returns the width.
  `TH_HASH_SMALL` sets it for a line of small text (the status bar): the kind word at the small size, the hash in Unifont 16 on the baseline of the kind word as the `Th_TextSmall` text beside it.
  With a `copyName` it is a copy link (hover tint, registered under that name, a click copies the full hash).
  Used by cards, rows, headings, the selection block, the info bar, the status bar, the glyph bar, the Usage and Similar lists, the Layers list.
- Owner of the full form: `Th_HashField(x, &y, w, caption, hash, name, what, flags)`: a caption (`Th_Section`) and a box of 28 px height holding the full hash in Unifont, hover tint and `TH_ACCENT` border, a click copies it and raises the toast "`what` hash copied".
  The field never wraps or truncates the hash.
  An empty hash reads "no ink", a NULL hash "not known".
  Used by the Font tab, the Typeface tab, the Glyph tab and the glyph view's Glyph tab.
- The clipboard, the toast and the hint live in `theme.c` (through the `Th_HoverFn` and `Th_NoticeFn` hooks that `main.c` sets); a view never calls `SetClipboardText` for a hash.
- A font or typeface without a name shows its short hash in place of the name; a name is never built from a hash.
- Nothing else prints a hash.

#### Push button

- Owner: `Th_Button(r, label, flags, hint)`; `Th_ButtonW(label)` is the width of a button holding the label (text measured plus `2 * TH_PAD`); a larger width is the caller's choice.
- Look: `TH_ROW_H` high, body text centred, `TH_PANEL_ALT` fill, `TH_LINE` border.
- States: hover `TH_PANEL` fill with `TH_HOVER_BORDER`; pressed `TH_PRESSED_FILL` with `TH_ACCENT` border and text; disabled dim text and no reaction; active `TH_ACCENT_TINT` fill with `TH_ACCENT` border and text.
- A one-shot command; it keeps no state.
- Flags, the only variations: `TH_BTN_DISABLED`, `TH_BTN_DANGER`, `TH_BTN_PRIMARY`, `TH_BTN_ACTIVE`, `TH_BTN_ICON(TH_IC_x)`.
- Primary (`TH_BTN_PRIMARY`: accent fill, white text) is the one main action of a surface: the confirming button of a dialog, the Duplicate to Edit of a bar, a card, a row or the selection block.
  A role that is primary in one place is primary everywhere (see "Duplicate to Edit"); a surface never shows two primary buttons for different commands.
- Icon button: an icon in place of the text (previous and next, tools, move arrows, eye, close, disclosure chevron).
  It has no fill and no border at rest, the same states otherwise.
  Its label is only its name for scripts, its `hint` is the tooltip.
- `hint` is offered to the view through `Th_Tip` while the mouse is over the button, also when it is disabled: the status line of `main.c`, the tooltip of the glyph view.
- Registers its label (the text before two spaces).
- No view draws a button of its own and no view has a button helper that draws.

#### Danger button

- Owner: `Th_Button` with `TH_BTN_DANGER` (the macro `Th_ButtonDanger(r, label)`); the same geometry and states as the push button with `TH_DANGER` label and border, a `TH_DANGER` tint when hovered or pressed.
- With `TH_BTN_PRIMARY` too it has a `TH_DANGER` fill: the confirming button of a destructive dialog.
- Used for every delete command, and only for those.

#### Lock

- Owner: the macro `Th_Lock(r, locked, hint)` in `theme.h`: a `Th_Button` with a padlock icon (`TH_IC_LOCK`, `TH_IC_UNLOCK`).
- Its script name is `@Unlock` while the thing is locked and `@Lock` while it is unlocked.
- Every command that makes something editable is this button: the info bar of the font view, the glyph view's bar, the cards, rows and selection block of the library views, the Typeface view's heading and its font rows, the Typeface tab.

#### Toggle button

- Owner: `Th_Toggle(r, label, &on, flags, hint)`, a `Th_Button` that shows `TH_BTN_ACTIVE` while `on` and flips it on a click.
- Look: the push button when off; on has `TH_ACCENT_TINT` fill, `TH_ACCENT` border and text.
- It returns true when it changed.
- Used by Metrics, Captions, Checker, All, the sample presets of the library views.

#### Segmented control and tab bar

- Owner: `Th_Segmented(r, &(ThSeg){ labels, n, sel, disabledMask, prefix, weights, hints })`.
- One look for the navigation bar, the side-panel tabs, the sort selectors and the value choices (Columns): a `TH_TRACK` track, `TH_ROW_H` high, the selected segment raised (`TH_PANEL`, `TH_LINE` border, `TH_ACCENT` text), body text.
- States: normal (dim text), hover (a lighter plate and `TH_TEXT`), selected, disabled (bit of the mask, `TH_TEXT_OFF`).
- `weights` are the relative widths of the segments (the navigation bar by its text, Columns gives Auto more room); no weights means equal widths.
- Segment i registers `<prefix>_<label>` and `<prefix>.<i>`.

#### Text field and search field

- Owner: `Th_TextField`.
- Look: 28 px high, `TH_PANEL` fill, `TH_LINE` border; focused has the `TH_ACCENT` border and a caret; the placeholder is dim body text; a search field is the same field with a placeholder and a clear mark.
- Keyboard: typed text, Backspace, Delete, Home, End, Enter commits, Escape reverts.
- Disabled shows dim text and takes no input.

#### Checkbox and switch

- Owner: `Th_Check` (a square box with a tick, label on its right) and `Th_Switch` (a pill with a knob) share one geometry helper.
- Check is for a setting in a form, the switch for an on/off state of a property; both are 28 px high rows with the label in body text.
- States: normal, hover, on, disabled.

#### Section caption

- Owner: `Th_Section(title, x, &y, w, flags, right)`: the caption text (semibold 13, dim, upper case) and a hairline to its right; it takes 24 px of height.
- `right` is small dim text at the right end (a count: "501 bitmaps, 1051 fonts", "39 fonts"); the hairline stops before it.
- `TH_SEC_OPEN` / `TH_SEC_CLOSED` make it a fold header: a chevron before the caption, a `TH_HOVER` plate on hover, and the call returns true when it was clicked.
  A fold header registers `<Title>_Fold` (`@Sample_Fold`).
- Used by every panel: the browser's side panel (Library, Filter, Sample, View), the Font, Glyph and Usage tabs, the Glyphs view's panel (Results, Character, View), the Layers panel and its Add Layer dialog, the Usage and Similar sections (fold headers), the sample strip header (a fold header), and the full-hash field's caption.
- A panel never writes its own heading look: no body-text title, no small-caps label of its own.
  Dialog and view titles are the heading role (`Th_TextHead`), not a caption.
- The inline group labels of a toolbar (INK, MOVE, ADVANCE) are the same caption face through `Th_TextCap`, the primitive under `Th_Section`.

#### Label/value row

- Owner: `Th_Row(x, &y, w, label, value, flags)`: the label (dim body) at the left, in a label column of `TH_ROW_LABEL_W` (108); the value (body, `TH_TEXT`) right-aligned at the right edge; `TH_ROW_UNI` shows the value in Unifont (code points).
- A value that is too long for the column ends in an ellipsis (`Th_FitText`).
- With `value` NULL the row is for a control: the label stays at the left and the function returns the rectangle of the value part, where the caller draws a text field, dropdown, segmented control, stepper or number box.
  A control row is `TH_ROW_H` high plus half a gap, a value row `TH_ROW_H`.
- Used by the Font, Typeface and Glyph tabs (`GlyphFacts`, the Typeface fields), the selection block of the font view, the browser's Style, Format, Zoom, Columns and Sort by rows, the Glyphs view's Zoom and Sort by, and the Layers card (Opacity, Offset X, Offset Y).
- Not rows: switches and checks (`Th_Switch`, `Th_Check` carry their own label), the status line, and the pinned selection block of the browser (a name, a detail line and a hash, as text).

#### List row and card

- Owners: `Th_ListRow(r, flags)` for a row of a list or inside a card, `Th_Card(r, flags)` for a card.
  Both draw the surface only and return its opaque fill, which the caller gives to what it draws on it (the fade of a sample); the content is drawn by the caller inside `r`.
- Card: `TH_PANEL` fill, `TH_LINE` hairline border, radius `TH_RADIUS`.
  Hover has the `TH_HOVER_BORDER`, selected the `TH_ACCENT` border on the `TH_SELECTED_FILL`, `TH_LR_WITHIN` a faint accent border (the card holds the selected row or is the current layer), disabled the `TH_PANEL_ALT` fill with a soft border (an empty state, a layer whose glyph is missing).
- Row: no border; hover is a `TH_HOVER` plate, selected is `TH_SELECTED_FILL` with a 3 px `TH_ACCENT` bar at the left, disabled reacts to nothing.
- The caller passes the state in `flags` (`TH_LR_HOVER`, `TH_LR_SELECTED`, `TH_LR_WITHIN`, `TH_LR_DISABLED`) because a scrolled list computes its own hover.
- Used by: the Typefaces boxes (card, the header and the variants are rows), the Fonts cards, the Typeface view's font cards, the Glyphs view's cells (cards), the glyph grid cells of the font view (cards), the Layers cards and the font list of the Add Layer dialog (rows), the Usage and Similar rows, and the preview box of the Glyphs view.
- Names that scripts use stay: `@card:N`, `@hdr:N`, `@tv.open.N`, `@lay_row_N`, `@lay_font_N`, `@usage_row_N`, `@similar_row_N`, `@fcell:N`.

#### Text that does not fit

- Owner: `Th_FitText(s, face, maxW, out, cap)` copies `s` shortened with an ellipsis to fit `maxW` in the face (`TH_F_BODY`, `TH_F_SMALL`, `TH_F_MONO`, `TH_F_HEAD`); `Th_TextClip(s, x, y, maxW, colour, face)` draws it so.
- No view has a fitting helper of its own.

#### Toast

- Owner: `Th_Toast(kind, fmt, ...)` pushes a message, `Th_ToastDraw(area, left)` draws the queue; `Th_ToastCount` and `Th_ToastHas` are for the scripts.
  Kinds: `TH_TOAST_INFO` (accent bar), `TH_TOAST_OK` (green bar), `TH_TOAST_ERR` (`TH_DANGER` bar, stays 6 s instead of 3.2 s).
- Look: a raised surface (`Th_Raised`) 36 px high, a 3 px bar at the left, body text, one line; a toast wider than the area ends in an ellipsis.
  Up to four are stacked upwards from the bottom of `area`, 8 px apart, and each fades in and out.
- Place: the bottom right of `area`, the bottom left when `left` is set.
  `main.c` passes the window without the browser's side panel (its Delete buttons stay clear) and, in the Typeface view, the left end (the row buttons are at the right).
- A view never keeps a toast queue of its own, and a command reports through `Th_Toast`.

#### Popover and menu

- Owner of the surface: `Th_Raised(r, alpha)` is what everything that floats looks like: a soft shadow, `TH_PANEL` fill, `TH_LINE` hairline border, radius `TH_RADIUS`.
  It is the frame of the menus, the Add Layer popover, the all-characters popup of the glyph view, the dialogs (confirm, export, progress) and the toast.
- Owner of the menu: `Th_Menu(&(ThMenu){items, n, .x, .y, .w, ...}, &open)` draws the surface and its items and returns the index picked, else -1.
  It clears `open` on a pick, on Escape and on a click outside (a click on `.keep`, the button that opened it, does not close it).
- Look: items are `TH_ROW_H` high with 4 px around them, body text 10 px from the edge; hover is a `TH_HOVER` plate; the shortcut of an item (`.key`) is small dim text at its right end.
- Item flags: `TH_MI_OFF` (dim text, inert; its `why` text goes to the status bar while it is hovered), `TH_MI_DANGER` (`TH_DANGER` text: a delete command), `TH_MI_SEP` (a hairline after the item), `TH_MI_CUR` (the current choice of a dropdown: `TH_ACCENT_TINT` and `TH_ACCENT` text), `TH_MI_DIM` (a placeholder choice such as "Any").
- The menu is kept inside the window; `TH_MENU_RIGHT` makes `.x` its right edge (the "..." menu).
- A long list (the Style and Format dropdowns) passes `.get` and `.ud` instead of `.items`, `.vis` (the rows shown) and `.scroll`; the wheel scrolls it and the scroll bar (see "Scroll bar") shows where it is.
- An item registers `.name`, or its label when it has none (`@file.save`, `@Edit_Glyph`, `@Ctx_Open`).
- Used by the top Menu button, the "..." menu of the font view, the context menu of the glyph grid, the context menu of the library views, the Style and Format dropdowns (the closed box is `Th_Dropdown`).
- Keyboard, for every menu and dropdown, in `Th_Menu` alone: Up and Down move a highlight (no wrap, disabled items are skipped), Home and End jump to the first and last enabled item, Enter picks the highlighted item, Escape closes.
  The highlight is the hover look (`TH_HOVER`); the mouse moves it when it moves over an item; a menu starts without a highlight, and Up from nothing goes to the last item.
  A long list keeps the highlighted row in view (it scrolls `.scroll`).
- While a menu is open Up, Down, Home, End, Enter and Escape are the menu's: `Th_MenuKeys` says so and `IsKeyPressed` / `IsKeyPressedRepeat` are wrapped in `theme.h` to answer false for them, so no shortcut or view reacts to the same key (`Th_Menu` reads the raw functions).
  `Th_MenuHi` is the highlighted index, for scripts (`assert menuhi N`); `tests\e2e\93_menu_keys.txt` tests it.
- Not yet: checkmarks, submenus.

#### Status bar

- Owner: `Th_StatusBar(r, reserve, left, nl, right, nr)`: a `TH_ROW_H` strip, `TH_PANEL_ALT` fill, a hairline above, small text.
- The left parts run from the left edge (12 px padding, 14 px between parts), the right parts end at the right edge; `reserve` is room at the right end that the caller fills with a control of its own.
  A part that does not fit is dropped, not clipped.
- A text part (`ThPart.text`) has a colour (default `TH_TEXT_DIM`) and at most `maxFrac` of the width; with `TH_SP_TAIL` it is cut with an ellipsis at its start (a path keeps its file name), else it is dropped when it does not fit.
- A hash part (`text` NULL) is `Th_HashShort(kind, hash)` with `TH_HASH_SMALL`, so it lines up with the 13 px text around it; `TH_HASH_ACCENT` for a changed state and a copy link when `copy` is set.
- Users: the application's bottom bar (`main.c`: the file and whether it is modified or saved, then the library, the font or the glyph finder, and at the right the hint of the hovered control or the glyph under the mouse), and the glyph view's line (`editor.c`: position, tool, ink, zoom and bitmap at the left, the glyph hashes at the right, the Checker toggle in the reserved end).
- The library views have no bar of their own: the application's bar is theirs.

#### Scroll bar

- Owner: `Th_ScrollBar(view, &scroll, content, step, live)`; `Th_ScrollRows(view, &first, n, vis, rowH, live)` is the same for a list of rows (the menu, the Add Layer font list).
- `view` is the scrolled rectangle, `content` the height of what scrolls in it, `scroll` the offset in px (0 at the top).
  The function clamps `scroll` to `0 .. content - view.height` in whole pixels, so a view keeps no clamp of its own.
- Look: a track at the right edge of `view`, 8 px wide and inset 4 px (`TH_SB_W`, 12 px, is what the bar takes: content stays clear of it); the thumb is a pill, at least 36 px high.
  At rest the thumb is 4 px wide in `TH_SCROLL_THUMB`; when hovered or dragged it is the whole 8 px, in `TH_SCROLL_THUMB_HOT`, over a `TH_SCROLL_TRACK` strip.
  No bar is drawn while the content fits.
- Input: the wheel over `view` scrolls `step` px a notch (0: the view takes the wheel itself, as the font grid does for Ctrl+wheel zoom); a wheel taken by a number box or an inner bar this frame (`Th_WheelUsed`) is not taken again.
  Dragging the thumb scrolls; a click on the track beside the thumb centres the thumb there and goes on as a drag.
  One bar is dragged at a time.
- `live` false (a popup is open over the view) gives the bar no input; it is still drawn.
- A menu moves its first row to keep the keyboard highlight in view; the bar follows (`Th_ScrollRows`).
- The result is true while the mouse is on the bar or drags it: the view must not hover or click what lies under it then.
- A view that needs the scroll before it draws calls the function before its content; the sample editor and the panels call it after the content they draw under the bar.
- Used by the font grid, the Typefaces, Fonts and Typeface views, the Glyphs view, the side panels (the browser's, the font view's, the glyph view's), the Usage list of the Glyphs view, the sample editor and the sample preview, the font list of the Add Layer popover, and the long menus (the Style and Format dropdowns).
- No view keeps a drag state, a thumb calculation or a bar drawing of its own.

### Colours by role

No view code holds a colour literal: every colour is a `TH_*` token in `theme.h`, or computed from one (`Fade`, `th_mix`, a colour that is data such as a glyph pixel or a layer colour).
A role that two views share is one token; near-duplicate greys were merged.

| Role | Token |
| --- | --- |
| Window, panel, caption strip | `TH_BG`, `TH_PANEL`, `TH_PANEL_ALT` |
| Hairline, faint line, checker square | `TH_LINE`, `TH_LINE_SOFT`, `TH_CHECKER` |
| Segment track, progress track, transparency swatch square | `TH_TRACK` |
| Slider track, hidden-layer chip, slider knob outline | `TH_LINE`, `TH_TEXT_OFF` |
| Text, dim, off, hint, warning, note, error, on accent | `TH_TEXT`, `TH_TEXT_DIM`, `TH_TEXT_OFF`, `TH_TEXT_HINT`, `TH_TEXT_WARN`, `TH_TEXT_NOTE`, `TH_TEXT_ERR`, `TH_TEXT_ON` |
| Content ink (sample text, browser renderings) | `TH_INK` |
| Modal veil | `TH_SCRIM` |
| Dark tooltip and its text | `TH_TIP_BG`, `TH_TEXT_TIP` |
| Read-only notice bar | `TH_NOTICE_BG` |
| Selected text | `TH_SELECTION` |
| Hover border (sample field, similarity shape box) | `TH_HOVER_BORDER` |
| Text on a colour swatch (light / dark swatch) | `TH_ON_LIGHT`, `TH_ON_DARK` |
| Layer palette | `TH_DANGER`, `TH_ACCENT`, `TH_M_CAP`, `TH_M_XHEIGHT`, `TH_M_UNDERLINE`, `TH_LAYER_PINK`, `TH_LAYER_TEAL`, `TH_TEXT` |

- The name chip over a glyph box is `TH_PANEL` at 90 % (`Fade`), the baseline in a finder cell is `TH_M_BASELINE` at 31 %, the knob shadow is black at 9 %.
- The check: `grep -n "(Color){" src/main.c src/browser.c src/editor.c src/finder.c src/layers.c src/insight.c src/sample.c src/panel.inc src/gpanel.inc` finds only zero-alpha `{0}` and colours computed from data.

### Markdown rule

This file and `HANDOFF.md` follow the author's Markdown protocol: a heading has one blank line before and after it, paragraphs are separated by one blank line, a list has one blank line before and after it, and every sentence is on a line of its own (also within a list item).

### Owners of the last round

| Component | Owner | File |
| --- | --- | --- |
| Lock button | `Th_Lock` (a macro over `Th_Button`) | `theme.h` |
| Hash at the status bar size | `Th_HashShort` with `TH_HASH_SMALL` | `theme.c` |
| Menu keyboard | `Th_Menu`, `Th_MenuKeys`, `Th_MenuHi` | `theme.c` |

### What changed in this round

- Duplicate to Edit is one primary button everywhere.
  The font view's info bar already was; the Typeface view and tab (`Duplicate Typeface to Edit`), the glyph view's chip (now a "Read-only" label and the button), and the browser's cards, rows, selection blocks and the Typeface view's font rows were plain or had a look of their own.
  The sites are `main.c` (info bar), `editor.c` (glyph bar) and `browser.c` (selection block, Typeface tab, Typeface heading, Typeface view rows, cards and list rows; the last two through one helper, `dup_button`).
- The status bar's hash label is drawn at the small size; the kind word and the 8 characters line up with the text beside them.
- Menus: arrow keys, Home, End and Enter, with a highlight that the mouse also moves.
- Colours: no change; the highlight is `TH_HOVER`.
- Deleted: `Th_Panel` (unused) and the raygui style set-up and include (no raygui widget is used).
- User interface files: 10947 lines (10960 before this round, 13 fewer).

### Acceptance of a conversion

A component is done when all of the following hold:

- Every use site of the role calls the owner function and none draws its own variant.
- The bespoke code is deleted and the line count of the user interface files has not grown.
- `build.bat test`, `build.bat e2e` and `build.bat` run without warnings.
- `tests\e2e\run_all.bat` reports 0 failed.
- The screenshots of every view that shows the component were looked at.

### Fonts and licences

- Noto Sans v2.015 (`assets/fonts/noto-sans`, all weights and italics, `OFL.txt`): SIL Open Font License 1.1.
  Used: Regular and SemiBold.
- GNU Unifont 18.0.01 (`assets/fonts/unifont`: `unifont-18.0.01.otf` for the BMP, `unifont_upper-18.0.01.otf` for the planes 1 to 16, `COPYING`, `OFL-1.1.txt`): dual licensed under the SIL Open Font License 1.1 and the GNU GPL version 2 or later with the GNU font embedding exception.

## Version 9: font layers

A font layer belongs to a FONT: every glyph of the font offers another font's drawing of the same code point as a layer.
Session layers (version 4) are unchanged and are never saved.

### Sources

- A font of the database (kind 0), as before: a pixel layer, no size.
- A file (kind 1), chosen in the Add Layer popover (source "File", the native dialog, filter in `flayers.h`):
  an outline font (`.ttf .otf .ttc .woff .eot .pfb`, `outline.h`) drawn as a smooth anti-aliased fill, or a bitmap font (`.bdf .pcf .fnt .fon .json`, the bitmap strikes of `.ttf/.otf`, `bmsrc.h`) drawn as pixels.
  The popover opens the file at once and shows what it holds.
  A `.ttc` face, a `.fon` font or a strike is chosen with a stepper (`face` of the layer).
  A `.ttf/.otf` that has outlines and strikes offers "Outlines / Bitmap strikes" (stored as `face >= 0x10000` for the strikes).
- WOFF2 is not supported.

### Placement and size

- The source glyph's baseline origin sits on the edited glyph's origin.
- The layer moves it by (x, y) pixels of the edited font (y up), an outline layer in quarter pixels, the others in whole pixels.
  A correction of the edited glyph (offset, size, hide) adds to that.
- Outline size: `fontPx * scale * dsize` pixels per em, `fontPx` = ascent + descent of the edited font (a font has no size metric), 100 % is the natural size (the em equals the pixel size).
- Bitmap size: whole multiples only, k = round(scale * dsize) from 1 to 16; 1 is the strike's own size (a strike cannot be shrunk).
- Rendering: an outline is rasterised at `size * canvas zoom`, uploaded as a texture when first drawn and cached (64 entries, 8 million pixels, least recently used first); above 1600 px per em it is not drawn.

### The Layers tab

Font layers and session layers share one stack (one list, one order UI).
A font layer carries the tag FONT, has the same controls as a session layer plus Size (a file layer) and a "THIS GLYPH" block: offset X and Y, size, "Hide for this glyph" and Reset.
Alt+drag moves a font layer's offset (every glyph).
A layer whose file is missing or unreadable stays in the list with its settings; its row says "file missing" or "cannot read: ...".
A file whose size or time changed is reloaded silently and the stored size and time are updated.
The file is looked at again every two seconds while the layer is used.
Order: only the order among the font layers is saved.
When a font is opened its layers are put at the bottom of the stack.

The Add Layer popover has a LAYER choice: "This glyph only" (a session layer, any source) or "Font layer" (saved with the font; the glyph is always the edited glyph's code).
"This font" as the source is always a session layer.
A file source defaults to a font layer, the other sources to a session layer.

### Where it is saved

The settings sidecar `<db>-settings` (`fsettings.h`, a SQLite file) holds the font layers by pxfm hash.
It is read when the database is loaded and written when the database is saved:

- Save writes the sidecar too.
  When only the settings changed, the document counts as modified (title, status bar, Save button, quit prompts) and Save writes only the sidecar.
- Save As writes a new sidecar next to the new file; the old one stays.
- Import starts with empty settings.
- Export to Folder does not touch them.
- With no layers at all nothing is written and an old sidecar is deleted.

An original may get layers (they are program settings, the .pxfdb is not changed).
The open font's layers live in memory (`FlDoc`) and are copied into the settings under the font's current hash whenever they or the hash changed.
A duplicate starts with the same layers (the same document); the first edit moves them to the new hash, and the earlier hash's settings are dropped unless it is the hash the document was opened with.
Committing a duplicate writes them under the hash it is committed with; the original's stay.
Until its first pixel edit a duplicate has the original's hash, so a layer added to it then is written under that hash, and the original shows it too (the same font has one set of layers).
A change of the layers is copied into the settings on the next frame, a change of the hash (an edit) at the commit and at Save.
A settings-only change makes the document unsaved (title, status bar, Save button, quit prompts) but does not change the store's own change counter.

### New Font and Add Glyph

Menu, New Font opens a dialog (name, style, pixel size, ascent, descent; the size is ascent + descent).
It creates an empty font with no glyphs and no typeface, as an editable document that has no original (it counts as modified).
It is committed like a duplicate.
The name is only the title of the session: an orphaned font has no name in the database.
"Add Glyph" in the font view's command bar adds an empty glyph for a code point (or without one).
Draw it in the glyph view over the layer.

## Version 10: importing a font file

A font file becomes a font of the database: Menu, Import Font File.
The file stays under the new font as a font layer, and the glyphs are drawn from that layer until they are edited by hand.
The searches and the drawing are in `src/fit.c` (no raylib), the document logic in `src/fitdoc.inc`.

### What can be imported

- An outline font (`.ttf .otf .ttc .otc .woff .eot .pfb`): its glyphs are drawn as pixels, as a pixel font or as a smooth font.
- A bitmap font (`.bdf .pcf .fnt .fon .json`, a bitmap strike of a `.ttf`): its pixels are copied as they are, glyphs without a code point are left out.
  It is always a pixel font, and nothing follows its layer.
- WOFF2 is not supported.

### The placement

A font layer of an outline file places the outline on the pixels of the font.
A point (X, Y) of the outline, in font units, lies at the pixel position `x = X * em * aspect / unitsPerEm + dx`, `y = Y * em / unitsPerEm + dy`.

- `em` is the size in pixels per em.
  It is an absolute size (new in version 10): the layer no longer depends on the font box, so the font box can follow the layer.
  A layer of version 9 has `em = 0` and keeps its size relative to the font box (`scale`).
- `aspect` is the width (100 % is square pixels).
- `dx`, `dy` are the offset in pixels.
  The buttons of their fields step by a quarter pixel, and any value can be typed (`Th_NumberF`, a comma counts as a point); the same holds for the size.
- A glyph may have its own correction (offset and size factor), as in version 9.

The same numbers draw the layer in the glyph view and draw the pixels of the glyphs, so what is seen under a glyph is what its pixels were made from.

### Drawing pixels from an outline

Two modes (`mode` of the layer):

- Pixel font: a pixel is inked when its centre is inside the outline.
  This is exact for a pixel font drawn with outlines, also when its pixels are drawn smaller or larger than a cell, or as dots.
- Smooth: a pixel is inked when the outline covers at least `threshold` of it (38 % or 50 % from the search, 5 to 95 % by hand).
  A glyph without an x correction is moved by a quarter pixel step (from half a pixel left to a quarter right) to where its pixels are closest to its coverage, so that stems fall on whole pixels.
  The layer is drawn moved by as much in the glyph view (`FlLayer_Shift`), so the outline lies where the pixels were made from.

The advance is the advance of the file in pixels, rounded.
The font box is the ascender and descender of the file in pixels, rounded.

### Finding the grid of a pixel font

`fit_find_grid` is a port of `pixelglyphlib/readers/ttf.py` (`find_grid`), with the same constants.
It was compared with the Python on the 621 TrueType and OpenType files of the real library: the same grid for all of them, 3 of them with a pitch that differs in the third decimal.

The searches, in order:

- dots: the font is drawn as separate dots of one size, the grid is the lattice of their centres;
- exact: the common divisor of every coordinate, when it gives at most 256 cells per em;
- coarser: the coarsest glyph divisor that is close to every coordinate or redraws 95 % of the ink;
- lattice: evenly spaced cells fitted to where ink starts and ends on each axis, the pixels may be shifted and need not be square;
- edges: the commonest edge lengths as grids;
- glyph columns: a lattice whose columns start anew in each glyph (stored as the x correction of each glyph);
- loose: a lattice for pixels drawn too large or carelessly, only the cores of the cells are compared.

What pxfe adds to the Python:

- When nothing passes, the best lattice that redraws at least 80 % of the ink is offered as a guess; the dialog says so and proposes the smooth mode.
- The search is fast enough for the dialog: 20 ms for a typical pixel font, 0.8 s for the 57 000 glyphs of GNU Unifont.
- The comparison found a fault of `outline.c`: a TrueType glyph stands `lsb - xMin` to the right of its own coordinates (the first phantom point), which the reader ignored.
  It is corrected for simple glyphs; a composite glyph stays where its parts put it, as fontTools draws it, so that the grids agree with pixelglyphlib.

### Finding the size of a font that is no pixel font

`fit_find_size` tries the sizes from 5 to 40 pixels per em in half pixel steps, each with four vertical positions (quarter pixels) and two thresholds.
The letters and digits are the sample (62 glyphs, or the first 80 glyphs with ink when the font has few of them).

- Each sample glyph has a true shape: its parts and its holes, counted on a large rendering (200 pixels per em).
- At a size, a glyph is right when its pixels have the same number of parts and of holes: the counter of an A is open, the dot of an i stands apart.
- The cover of a size is how close the pixels are to the coverage of the outline: `1 - sum |coverage - pixel| / sum max(coverage, pixel)`.
- The smallest size at which nearly as many glyphs are right as at any size is the base (at most 3.5 % fewer, and with a small sample at most one and a half glyphs fewer).
- Among the base and the sizes up to two pixels above it, the one with the best score (right glyphs plus half the cover) is taken: there stems and heights tend to fall on whole pixels.

At the size taken, the position is then searched in sixteenths of a pixel with seven thresholds (30 to 55 %); the coarse answer stays unless a finer one scores better.

The search takes about half a second for a font with all letters and digits.
The size can be set by hand, to any value: in the dialog, and later with "Optimize for This Size" in the Layers tab.
The position and the threshold are then searched for that size (about 50 ms); the width and the offset X stay as they are.

### The dialog

Menu, Import Font File opens the native file dialog, then the Import Font File dialog:

- Family and Style (from the file, editable) and the face of a collection or the strike of a bitmap font;
- the switch "This is a pixel font": on when a sure grid was found, off otherwise; it cannot be switched on when no grid was found, and a bitmap font is always one;
- what the search found (the grid and how it was found, or the size, the font box and how many sample glyphs keep their shape);
- a preview of a sample drawn as the import will draw it;
- for a smooth font the size in half pixels and "Best Size";
- "Make a typeface with these names".

Import makes a new document (no original, like New Font): the font, the typeface (one variant with the names of the file: family, style, full name, version, copyright, designer, licence and so on; HASH is the hash of the file), and the font layer.
A typeface whose text the strict reader would refuse is first cut down to family and style, and left out when even that fails.
The document is committed into memory like a duplicate, and Save writes the layer into the settings sidecar under the hash of the committed font.

### Glyphs that follow a layer

One font layer of a font may have `follow` set.
It must be an outline file with an absolute size.

- When the size, width, offset, mode or threshold of that layer changes, or the correction of a glyph, every glyph that still is what the layer drew before is drawn again (`FitTick` in `fitdoc.inc`, once per frame).
- A glyph edited by hand is not what the layer drew, so it stays as it is.
  Nothing is stored for this: the earlier drawing is made again from a copy of the layer's settings at the last redraw and compared (ink, position, advance).
- The font box follows while ascent and descent are what the layer gave.
- "Redraw All" draws every glyph from the layer again, also those edited by hand.
- Undo and redo take the layer's settings along with the font, so the two never disagree.
- An original is read-only: the placement of the layer its glyphs follow cannot be changed (the fields are off, a change made elsewhere is put back with a notice), or a duplicate could no longer tell which glyphs the layer drew.
  Colour, opacity and visibility stay program settings; a duplicate follows as before.
- Switching "Draw the glyphs from this layer" on redraws nothing: the font as it is is the starting point.
  A layer with a relative size gets its present size as the absolute size.

### Where the settings are

The font view's panel has a fourth tab, Layers.
It lists the font layers of the open font.
For the layer the glyphs follow (or the first outline layer) it has Size (px/em), Width (%), Offset X and Y, the follow switch, Smooth or Pixel font, Threshold, "Optimize for This Size" (smooth mode), "Find Best Fit" (the search of the import again, for the mode chosen) and "Redraw All".
It tells how many glyphs follow, are edited by hand, or are not in the file.

The glyph view's Layers tab edits the same layer: its offset and its size (px/em for an absolute size) for the whole font, and the correction of the edited glyph.

### Storage

The settings sidecar has schema version 2: the `layer` table gets `em`, `aspect`, `follow`, `mode` and `threshold`.
A version 1 file is upgraded in memory when it is opened and on disk at the next save.

### Not done

- Kerning, cap height and x-height of the file are not imported.
- A family of several files is not imported as one typeface with several variants: each file gives its own typeface.
- A negative advance (a glyph left of its origin) is imported as advance 0.
- When the face of the followed layer is changed, or its file changes on disk, no glyph is what the layer drew before, so all count as edited by hand until Redraw All.
- The searches run on the UI thread: the window does not answer for their length (under a second, except for very large fonts).

## Version 11: the default glyph

The default glyph (the one drawn for a code the font does not have) is a glyph of its own with the code `FFFFFF`.

- It is a normal `PxGlyph` (bitmap, offsets, advance), sorted last by code, written as a `GLYPH FFFFFF` line of the pxfm and stored as a `pxfm_glyph` row (no schema change).
  The old `_DEF` pointer to another glyph, and its "dangling" variant, are gone.
- The grid shows it in the last place with the caption "Default" (its code `FFFFFF` shows on hover).
  It can be edited in the glyph view, opened from the grid and deleted.
- "Set Default" copies the bitmap, offsets and advance of the selected glyph into it: an independent copy, so deleting or editing the source does not change it.
- It never collides with the uncoded glyphs `F00000` to `FFFFFE`, and it is not counted among the glyphs of a font (the font's glyph count and the uncoded count skip it).
- Load, Save, Import, Export, clone, delete and the cleaning of orphans keep it, because the font's pxfm names its pxgl.

### The bugs of the old model

- The default was only a pointer to a coded glyph, so it was not a glyph of the grid and could not be edited.
- Deleting the glyph it pointed to dropped the default.
- An uncoded glyph could not be the default.
- A dangling default carried no bitmap.

## Version 12: locks

Replaces "duplicate to edit" (a working copy that was saved as a new font next to the original).
Every font and every typeface has a lock, locked by default; what is unlocked is edited in place.
The sections of versions 3 to 10 that speak of originals, duplicates, "Duplicate to Edit" and drafts describe the state before this version.

### Where the lock is stored

The settings sidecar (`<db>-settings`) has schema version 3: a table `unlocked(kind, hash)`, kind 0 for a font and 1 for a typeface.
Only unlocked things have a row.
A version 1 or 2 file is upgraded in memory when it is opened and on disk at the next save.
A change of a lock is a change of the settings: it counts as unsaved until Save, like a layer.

### Editing in place

`store_edit_in_place` (in `store.c`) takes the open font (and typeface) and the hashes the store holds for them.

- The edited font is added under its new hash.
- Every typeface that names the old font is rewritten under its new hash (the variants name the new font), through the machinery of the deleting plans (`plan_orphans`, `store_apply_delete`).
- The old font is removed, and so are the glyphs that no font names any more.
  The old font cannot be needed by something else once every typeface naming it is rewritten; when the edit leaves the hash as it was there is nothing to do.
- The result lists the typeface hashes that moved, and `main.c` carries them to the sidecar (`fs_rename_font`, `fs_rename_typeface`: layers, corrections, the lock) and to the open views and navigation.
- A typeface field edit rewrites the typeface under its new hash the same way.

### The open font

- The document stays in memory across a commit, so undo and redo keep working.
- `origHash` and `origContHash` are what the store holds; they move at each commit.
- A font that is not in the store yet (New Font, Import Font File) is added at its first commit and is unlocked.
- Save, Load, Import, Export and Save As work as before.

## Version 13: importing a picture of a font

Menu, Import Font File also takes a picture: PNG, GIF (its first frame), JPEG, BMP, TGA.
A picture tells nothing about itself, so the importer works out the background, the scale, the layout, the characters and the colours, and the dialog lets every one of them be set by hand.
The analysis is in `src/sheet.c` (no raylib), the reference glyphs in `src/sheetref.c`, the dialog and the making of the font in `src/fitdoc.inc`.

### Background and ink

- A picture with transparent pixels (more than 2 % of it): they are the background, every other pixel is ink.
- Else the commonest colour is the background, and every pixel of another colour is ink.
- A picture with more than 300 colours has noise (a JPEG): the background is the commonest colour counted coarsely, and a pixel is ink when it differs from it by more than 60 in a channel.

### Scale

A preview draws every font pixel as a block.
The scale is the largest s (up to 24) for which nearly every change of colour along the rows and the columns lies on a lattice of pitch s (98.5 %, with noise 86 %).
The picture is then brought back to one pixel per font pixel, taking the pixel in the middle of each block (with noise: the mean of the block).

### Layout

Three layouts, tried in this order:

- Marker row: a strip whose top (or bottom) row holds single pixels of one colour.
  Each marker stands over a column that separates two glyphs: a glyph is the columns between two markers, and the marker's own column belongs to no glyph.
  A pixel of another colour in that row is ink of a glyph.
  Ink under the next marker that touches the glyph (a shadow that spills over) stays with that glyph.
  What follows the last glyph (a closing marker at the edge) is dropped.
- Grid: cells of one size.
  The width is the smallest pitch for which every stretch of ink between blank columns lies inside one cell; a pitch a little larger that divides the width of the picture, with no margin, is taken instead when there is one.
  Stretches far longer than most (glyphs that touch) do not count.
  The height comes from the text rows in the same way.
  When the glyphs fill their cells and leave no gaps (plates, outlines that touch), the period of the picture tells the cell: the smallest divisor of the size at which the picture repeats, for the height the one closest to the width.
  A grid whose cells are mostly blank is no grid.
- Rows: text rows cut where a row of pixels is blank, glyphs cut where a column is blank.
  A band far lower than the others (dots, accents) joins its closest neighbour.
  A stretch about twice as wide as most is two glyphs that touch, and is cut where the fewest pixels are inked.
  A picture with noise is always read as rows.

A sheet that holds the same glyphs several times (one font in several colours) is told by cells that repeat exactly; one copy is imported, the dialog chooses which.

### Characters

Sheets list their characters in runs of ascending codes: ASCII from the space or from `!`, the upper half of code page 437 after `~`, capitals then digits, `1` to `9` then `0`.
So the characters are not read one by one, but as the best sequence.

- Each glyph is compared with the same character of reference fonts: GNU Unifont and Noto Sans Regular, Bold and Black, about 900 glyphs, made once (two seconds).
  The shape is compared on a grid of 8 x 10 after stretching the ink box to it, and the place in the line (top, bottom and width in line heights) counts too, which tells `,` from `'` and `o` from `O`.
- A glyph of several colours (an outline, a shadow, a bevel) is read from all its ink and from single colours of it, alone and left out, and keeps its best reading.
- The sequence is found by dynamic programming over the cells: a glyph that continues the run of the one before it gets a bonus (0.5, a full match is 1), a higher code a small one.
  A blank cell holds the place of a missing character, or no place at all (the end of a row).
- Capitals standing in the places of the small letters are accepted there.
- A code given twice stays with the glyph that stands in a run; the other takes its best code that is still free, or none.
- The candidates are ASCII, Latin-1, the upper half of code page 437 and a few more (the euro sign, arrows, the ligatures of French).

A first character can be typed in the dialog; the glyphs are then counted from it, cell by cell, and nothing is recognised.

On the 49 pictures of the test collection the layout is found for all, and 97.4 % of the 3843 characters whose truth is known get the right code.

### Colours and metrics

- The colours of the ink become the palette of the font, at most 36; when there are more (a JPEG), the pair that costs least to merge is merged until 36 are left.
- A font of one ink colour is imported black on white, without colour lines; a font of several on a transparent background.
- The baseline is where most glyphs end; ascent and descent are how far the ink reaches above and below it.
- In a grid and in a marker strip the advance is the width of the cell and the glyph keeps its place in it; a grid makes a monospaced font.
  In rows the advance is the width of the ink and a gap of a twelfth of the height.
- A space is added when the sheet has none.
- A glyph left without a character is kept, without a code point.

### The dialog

The dialog of a picture has Family and Style, the layout (Auto, Grid, Rows, Marker row; what was found is told beside it), the cell of a grid, the scale, the copy of a sheet with several, the first character, "One colour", what was found in words, and a preview of every glyph in its colours over the character it will get (amber when the match is weak).
Every change runs the analysis again.

### Not done

- The picture is not kept as a layer under the font.
- A photograph of lettering is cut into glyphs and read, but its glyphs come in at the size of the picture and in up to 36 colours; they are not brought down to a pixel grid.
- Kerning is not guessed.
- Sheets whose characters follow no run at all (punctuation in the designer's own order) get wrong codes for those glyphs; the preview shows them, and they can be changed in the font view.

## Version 14: the baseline, and the facts of a glyph

Two edits of an unlocked font that were missing: moving the baseline, and setting a glyph's code point, advance and offsets by hand.
Both are in-place edits like every other (version 12): one undo step each, the font's hash changes, the commit follows at the usual places.

### The baseline

The Font tab's FONT METRICS section has a Baseline row under Descent: two icon buttons, up and down (`@Baseline_up`, `@Baseline_down`).
The baseline moves one row and everything else stays where it is on the screen.

- Up by 1: the ascent falls by 1 and the descent grows by 1.
  Down is the mirror.
- Every other stated height counted from the baseline moves with it: cap height, x-height, underline position and strike-out position fall by 1 when the baseline goes up.
  Thicknesses, line gap, internal leading and quad width do not change.
  A metric that is not stated stays not stated.
- Every glyph with ink gets its offset y changed by the same amount (y is up from the baseline, so its bitmap stays where it is).
  A glyph without ink keeps its offset (0, 0).
- The font layers sit on the baseline's origin, so each moves its offset y by the same amount, and the layer the glyphs are drawn from moves what it drew them with, so none of its glyphs is drawn again.
- A step that would make a metric go below its smallest value (ascent, descent, cap height and x-height are never negative; the positions may be) is refused: the button is dim and its hint says why.
  The buttons are dim while the font is locked.
- One step is one undo step.
  The font layers are outside the font's undo snapshots, so the undo stack keeps beside each snapshot the net number of rows the baseline had been moved (`baseShift`); an undo or redo moves every font layer by the difference.
  A layer added after a step is moved by an undo of that step too.

### The facts of a glyph

In the glyph view's Glyph tab the code point, character, advance and offset x and y are fields (Th_TextField, Th_Number), dim and inert while the font is locked.
The font view's selection section shows the same facts as text.

- Advance: a number, 0 or more.
  Offset x and y: numbers, any sign; they are dim for a glyph without ink.
  A change is one undo step; steps on one field within 0.8 s are merged.
- Code point: hex digits, with or without `U+` or `0x`.
  Character: one character (UTF-8), or the word `space`; it sets the code point.
  A field is applied on Enter, Tab or a click elsewhere.
- Refused with a message (an error toast, nothing changes): a code another glyph of the font has (no silent swap), `FFFFFF` (the default glyph's code), a number above 10FFFF, text that is no code.
  The default glyph's fields for the code are dim: it cannot be given another code.
  An uncoded glyph can be given a code point; the later uncoded glyphs are renumbered as when one is deleted.
- The glyph keeps its bitmap, offsets and advance and moves to its place in the order (`px_font_remove_glyph` and `px_font_add_glyph`).
  The glyph view stays on it (`Editor_SetGlyphIndex`: the index changes, the view does not), and so does the selection.
- Kerning pairs that name the old code follow the new code (both sides of a pair; the other side is renumbered too when it is a later uncoded glyph).
- Undo and redo bring the glyph view back to the glyph by the code it had.

### Not done

- No keyboard shortcut for the baseline.
- The font layers added after a baseline step are moved by an undo of that step.

## Version 15: filtering by colours, typing while the mouse is away, press-and-release

Three small changes of the shared widgets and of the library views' filter.

### The Colours filter

The FILTER section of the library views' side panel has a third dropdown, Colours, under Style and Format.
It works in the Typefaces tab and in the Fonts tab, and in the Typeface view's lists the filter is not shown, as for Style and Format.

- The number of colours of a font is the number of ink colours it uses: the pixel characters other than `.` (the background) that its palette lists, and at least 1.
  A font without COLOR lines is a classic one-colour font, and so is a font whose palette lists only `.`.
  The count is read from `pxfm_color` with the other facts of the font (`StoreEntry.colors`), or from the loaded palette (`LibInfo.colors`), so it is known once the info of the font is.
- The values are Any and the counts that occur in the tab, as numbers in numeric order.
  The list follows the info that arrives while it is open: a font that states no ascent or descent is loaded for its info while the dropdown is open or a count is chosen.
  A chosen count that no font of the tab has is dropped only when the info of every font is known.
- A font whose info is not known yet does not match a chosen count.
- In the Typefaces tab a variant row matches when its font has the chosen count, and a typeface box is shown when any of its variants matches, with the variants that match.
  This is the rule of the Style and Format filters too.
- The choice is kept in the `[browser]` section of the settings file (`colors=`) and cleared by Clear filters.
- Script names: `@Colours_Dropdown`, `assert bcolors` (0 is Any), `assert bcolorvals`, `assert bcolorval<i>`.

### Typing while the mouse is away

A focused text field takes its keys unless a modal dialog or a popover blocks the input.
Before, the panels blocked the scrolled widgets with the same flag when the mouse left their scroll area, so a field stopped taking keys as soon as the mouse wandered off.
`theme.h` now has two gates:

- `Th_SetInputBlocked`: mouse and keyboard.
  Used for modal dialogs (New Font, Import folder and picture, Export, the unsaved prompt), the progress screen, the frame after a document opened (`swallowMouse`), open popovers and menus (the Style, Format and Colours lists, the context menu, the file and "..." menus, the Add Layer popover), and in main.c while a panel field is being edited (the glyph canvas ignores the mouse and keys then).
  Every `Th_SetInputBlocked` call in `browser.c`, `main.c`, `fitdoc.inc` and `sample.c` is of these kinds.
- `Th_SetMouseBlocked` (`Th_MouseBlocked` reads both gates, `Th_MouseOnly` the second): the mouse only.
  Used by the code that draws a scrolled region for the widgets outside its visible part or under a pinned block: the browser panel's scroll area, the Usage and Similar lists (`insight.c`, `panel.inc`, `gpanel.inc`).
  A text field that has the keyboard keeps taking keys; a click elsewhere still ends the editing.
- The mouse-only readers (`hov` in browser.c, the editor's `hit`, the layers' `hov`, `cx_hover` of insight.c) read `Th_MouseBlocked`.
- `SampleEdit` already took only the modal and popover block (its `blocked` argument), so it was not affected.
- Ctrl+F in the browser focuses the filter as before.

### A press must begin on the control

A release-fired control used to act on any release over it, so a button that moved under the mouse (the first edit of a font adds a box above the metrics) was triggered by a press that began on another.
`Th_Released(rect, name, hover)` is true only when the press of this click began on the same control (same name and rectangle).
`Th_Button` (and so `Th_Toggle` and `Th_Lock`), `Th_Check`, `Th_Switch`, `Th_Segmented`, `Th_Dropdown`, a foldable `Th_Section`, the copy of a hash, the palette swatches, the glyph finder's cells, the usage and similar rows and the Add Layer popover's lists use it.
The stepper acts on the press, and a menu's items on the release of a press that began on the open menu.
The Baseline buttons lost their local workaround.

### Tests

`109_color_filter` (on `testdata/lib_colors.pxfdb`, a copy of `lib.pxfdb` with palettes for two fonts), `110_type_mouse_away` and `110b_type_mouse_away_glyph`, and `111_press_release`.

### Not done

- The Colours count is of palette entries, not of distinct RGBA values: two pixel characters with the same colour count twice.

## Version 16: selection and moving

The glyph view gets the selection tools of the common pixel editors: a rectangular marquee, a lasso, a magic wand and a Move tool, with the commands that go with a selection (select all, deselect, invert, delete, cut, copy, paste, move by the arrow keys).
The behaviour is modelled on common pixel editors; the code is written for pxfe.
The line, ellipse and fill tools of the same family come later; this version only prepares the table they join.

### The tool table

`editor.c` has one table, `kTool[]`: name, shortcut as shown, raylib key, icon, group and whether it is a selection tool.
The tool strip (version 18), the shortcuts, the status line and the tooltips all read it, so a new tool is a row in the table, a `Tool` value, an icon in `theme.c` and, when it works with the mouse, a branch in `canvas_input`.
The drawing tools are as before; the selection tools (group 3 since version 18) follow after a gap.

| Tool | Key | Value of `etool` |
| --- | --- | --- |
| Pencil, Eraser, Line, Rectangle, Fill | B, E, L, R, F | 0 to 4 |
| Marquee | M | 5 |
| Lasso | Q | 6 |
| Magic Wand | W | 7 |
| Move | V | 8 |

Choosing another tool drops floating pixels (see below).
The tools are off, as the others, while the font is locked or the glyph's layer is hidden.

### The mask

`selection.c` and `selection.h` (no raylib, tested by `tests/test_selection.c`) hold a selection as a cell mask: a bounding box with one byte per cell, in glyph space (x from the pen, y a row counted down from the baseline, as in `editor.c`).
A non-empty mask is always tight.
The mask is not limited to rectangles, so add, subtract and intersect work.

- `sel_combine` meets a new shape with the selection in one of four modes: replace, add, subtract, intersect.
- `sel_invert` turns the cells of a rectangle (the glyph box across the advance, and the bitmap where it reaches out of it) into their opposite; selected cells outside the rectangle are dropped.
- `sel_polygon` fills a closed path: a cell is inside when its centre is (even-odd rule), and the cells the outline passes through are in too, so a thin loop or a line still selects something.
- `sel_wand` selects the cells joined to the clicked one that hold the same character, inside the same region (4-connected, or 8-connected).
- `SelBitmap`, `selbm_clear` and `selbm_stamp` read, clear and write a character bitmap; `SelFloat` is the lifted pixels with their shape.

### How a selection is made

- The mode comes from the bar (the segmented control Set, Add, Sub, And, script names `@selmode.0` to `@selmode.3`) and from the keys at the press: Shift adds, Alt subtracts, Shift and Alt together intersect.
  The keys win over the bar.
- Marquee: drag a rectangle of cells.
  Shift pressed after the press (not already down at it) makes it a square, from the start cell towards the pointer.
- Lasso: drag a free path; the cells inside it and on it are selected when the button goes up.
- Magic wand: click.
  The bar's toggle 8-way (`@8-way`, shown with the wand) also joins diagonal neighbours; the default is 4-connected.
  The region is the font box across the advance and the bitmap, so empty cells are a group too; a click outside it selects nothing.
- A click without a drag (the mouse moved less than 3 pixels) with the marquee or the lasso deselects, in Set mode, when it is outside the selection.
  A drag within one cell selects that cell.
- While a shape is dragged it is drawn in the accent colour; Esc cancels the drag.
- The selection is shown with marching ants: a 1 px dashed outline round the selected cells, dark dashes (`TH_TEXT`) and light dashes (`TH_PANEL`) that move, over a faint tint of the accent colour.
- Ctrl+A selects all (the glyph box across the advance and the bitmap), Ctrl+D deselects, Ctrl+Shift+I inverts.
- Esc deselects when a selection exists (and cancels a float first); with nothing selected it closes the glyph view as before.
- The selection is kept while the glyph view stays on the glyph, and goes with a new Editor_Open (another glyph, an undo or redo).
  Selecting is not an undo step.

### Moving: the float

A move does not change the glyph until it is dropped.
The selected pixels are copied into a float (`SelFloat`), drawn over the glyph with the cells they came from drawn empty (a copy leaves them), and the selection moves with the float.
The drop writes the result and is one undo step (`EditorBeforeChange` is called once, at the drop); a cancel leaves the glyph as it was.

- Lift: a drag that starts inside the selection with the marquee or the lasso in Set mode and with no Shift or Alt (in the bar's Add, Sub and And modes a press inside the selection changes the selection instead), a drag anywhere with the Move tool, the arrow keys, the bar's MOVE buttons.
  The lift happens at the first whole cell of movement, so a click inside the selection lifts nothing.
- The Move tool with no selection takes the whole bitmap (a rectangle of the bitmap's box); the selection goes away with the drop.
- Ctrl at the press makes the drag move a copy: the original stays.
  (Alt is the subtract modifier, so it is not used for this.)
- Whole cells only: the float is at the cell under the pointer, offset by the cell where the press began.
- Arrow keys with a selection nudge by 1 cell, Shift+arrow by 8 cells (`NUDGE_BIG`).
  Without a selection Shift+arrow moves the whole bitmap by 1 cell as before.
- The bar's four MOVE buttons do the same as the plain arrow keys when there is a selection (their tooltip says so), and move the whole bitmap by 1 cell as before when there is not.
- Transparency: a cell of the float that holds `.` does not overwrite what it lands on; only pixels other than `.` are written.
  A move first clears the cells it came from (the whole selection, `.` cells included), then writes the pixels.
- The drop: Enter, choosing another tool, a press outside the float (with the marquee or the lasso; the press also deselects), leaving the glyph (Back, previous and next glyph, the padlock, Save, Export and every other commit through `Editor_Flush`, the navigation).
- Cancel: Esc (it cancels the float, the next Esc deselects), Esc during a drag, and Ctrl+Z while pixels float (the pixels are thrown away, no undo step is taken); Ctrl+Y does the same.
- A drop that would change nothing (a move by 0 cells, a selection without ink) takes no undo step.
- Growth: the pixels may land outside the bitmap and the font box.
  The drop grows the bitmap the way the pencil does (`ensure`), writes, and then crops the bitmap to its ink (`px_glyph_crop`; the hash is that of the cropped bitmap anyway).
  The bitmap is limited to 4096 cells a side, as for the pencil; a drop that would exceed it is refused with a toast and cancelled.

### Delete, cut, copy, paste

- Delete and Backspace clear the selected cells to `.`: one undo step, none when no selected cell holds ink.
  With a float that is a move, the pixels vanish and the place they came from is cleared; with a float that is a copy or a paste they are thrown away.
- Ctrl+C copies the selected pixels (or the floating ones) into the clipboard of the program, Ctrl+X copies and deletes.
  The clipboard keeps the pixel characters and the cells they had, and lives for the session, so it also works into another glyph and another font; it is not the system clipboard.
- Ctrl+V pastes as a float at the cells the pixels were copied from.
  When they are not all in view, the pixels' top-left cell goes to the top-left cell of the view.
  The tool becomes the marquee when it was not a selection tool, so the float can be dragged.
  The paste is written by the drop (one undo step) and thrown away by Esc.
- A locked font (and a hidden layer) refuses Delete, Cut and Paste with the toast "The font is locked"; the selection tools are off, so there is nothing to select.

### The layers

The selection acts on the glyph's own pixels only.
The reference layers are not read or written.
Alt+drag with a reference layer selected still moves that layer (it was there first); use the bar's Sub mode to subtract then.

### Not done

- Undo of the selection itself (only the pixel edits are undo steps).
- Bar buttons for select all, deselect and invert (the keys only), and a single-cell selection with the marquee needs a drag of at least 3 pixels (the wand also does it).
- The selection does not limit the other tools' painting (changed in version 17: the drawing tools are clipped to it).
- The system clipboard (a paste from another program).
- Selection by colour range, feathering, rotation and scaling of the floating pixels.

### Tests

- `tests/test_selection.c`: 44 checks of the mask, wand, polygon, combine, invert, bitmap and float functions (in `build.bat test`).
- `112_select_move` (marquee, move by drag, drop, undo and redo, Esc, nudge, a press outside, Ctrl+drag copy, the Move tool, the tool switch), `113_select_modes` (select all, deselect, invert, square, Shift, Alt, the bar's modes, the lasso, the keys), `114_select_wand`, `115_select_clipboard` (delete, cut, copy, paste, transparency, another glyph, a paste out of view, a paste dropped by Back), `116_select_growth_locked` (a locked font refuses, Shift+arrow out of the box, the bitmap's growth and undo, PageDown, Back and the padlock drop the pixels).
- Play probes: `assert eselcount`, `efloating`, `eclip`, `assert esel bounds X Y W H` (`*` matches any X or Y), `esel cell X Y [0|1]`, `esel none`; `keydown shift` (and `ctrl`, `alt`) holds a key while the mouse moves.

## Version 17: line, curve and pixel-perfect

The glyph view gets the line and curve tools of the common pixel editors, a pixel-perfect option for freehand strokes, and drawing that is limited to the selection.
The behaviour is modelled on common pixel editors; the code is written for pxfe from textbook algorithms (Bresenham for the line, the cubic Bezier polynomial sampled at steps under one cell for the curve).
The rectangle, the ellipse and the fill of the same family come later (the rectangle and the ellipse in version 19); the rectangle and the fill are as before, except for the clip below.

### The raster functions

`src/raster.c` and `.h` (no raylib, no pxfont; `tests/test_raster.c`) hold the cell paths:

- `ras_line` is Bresenham.
  The line is walked from its smaller end (smaller x, then smaller y) and reversed when needed, so a line from A to B and one from B to A cover the same cells, and the path always runs from the first end to the second.
- `ras_snap` is the constrained angle, `ras_ends` adds the centre mode (see below).
- `ras_is_corner` and `ras_pixel_perfect` are the pixel-perfect rule.
- `ras_bezier` is the cubic curve.

### Line

Drag from the start cell to the end cell: the preview is drawn while the mouse is down, the line is drawn at the release, one undo step.
Esc during the drag cancels it.
The right mouse button draws with `.` (it erases), as for the pencil, the rectangle and the fill before.

- Shift constrains the angle.
  The vector from the start to the pointer is turned to the nearest multiple of 15 degrees and then to the clean pixel slope that stands for it: 0, 15, 30, 45, 60, 75 and 90 degrees become the slopes 0, 1:4 (14 degrees), 1:2 (27 degrees), 1:1, 2:1 (63 degrees), 4:1 (76 degrees) and 1:0, in all four quadrants.
  The end is cut to a whole number of steps of that slope (the nearest one to the pointer along it), so every constrained line has a regular stair: an exact 2:1 line is made of runs of exactly two cells.
- Ctrl draws from the centre: the press cell is the middle of the line and the far end is mirrored through it.
  Ctrl was picked because Alt is the subtract modifier of the selection tools and, with a reference layer selected, moves that layer.
  Shift and Ctrl together give a constrained line from the centre.
- The modifiers are read live: the preview and the line follow them until the release.
- Shift+click with the Pencil draws a line from the last cell the pencil or the line ended on to the clicked cell (one undo step), and the drag goes on as freehand from there.
  There is no last cell in a glyph view that was just opened, after an undo or a redo, or before the first stroke: Shift+click is then an ordinary click.

### Curve

The Curve is a tool of its own (`etool` 9): Shift+L chooses it, L chooses the line, and since version 18 it has its own button in the tool strip.
In version 17 it was a hidden row of the table, a variant of the Line button with a Line / Curve control in the bar's second row; version 18 removed both.

The curve is a cubic Bezier with two end points and two control points, made with three drags:

1. The first drag sets the two end points, like a line (Shift and Ctrl work as for the line).
   The preview is the straight line.
   A click without a drag sets nothing.
2. The second drag pulls the first control point to the cell under the pointer (where the drag ends), the third the second one.
3. The curve is drawn when the third drag is released, or on Enter at any point after the first drag; the control points that are not set yet lie on their end points.
   Esc cancels (and so does Ctrl+Z); choosing another tool, leaving the glyph, the padlock and every commit of the application draw it as it is.

While the curve is being made the glyph does not change: its cells are drawn as a preview, with the handles (a square for each end point, a circle for each control point, a line from each end point to its control point).
The right button draws the curve with `.`; the second and third drag must use the same button as the first.
The path is 8-connected with no gap and no cell twice in a row, and it has no L corner (the pixel-perfect rule is always applied to a curve).
The drawing is one undo step; it grows the bitmap as the pencil does.

### Pixel-perfect

The bar's Pixel-perfect toggle (second row, shown for the Pencil, the Line and the Curve) is off by default.
When it is on, a freehand pencil stroke is kept one cell wide: when three consecutive cells of the stroke make an L (two steps along different axes, the first and last cell touching diagonally), the middle cell is taken back.
The cell goes back to what it held before the stroke painted it, so a cell that was inked before the stroke stays inked, and the right button's erase puts the ink back.
The rule works while the stroke is drawn, and the bitmap is cropped at the end of the stroke when it left an empty margin.
Lines are never made of L corners, and the curve always applies the rule, so the toggle changes the pencil only; it is shown for the Line and Curve so that the option is in reach while drawing.
The choice is kept in `pxfe.ini` (`[app] pixelPerfect`, 0 or 1), as the other editor options.

### Drawing inside the selection

When there is a selection (version 16), the Pencil, the Eraser, the Line, the Curve and the Rectangle draw only inside it: the cells outside are left alone, the preview does not show them, and the stroke is still one undo step.
The selection is not changed by drawing.
The Fill was not limited in this version; since version 20 it is clipped too (see "Version 20: fill, eyedropper, polygon and contour").
Pixels that float from an arrow key land (one undo step) when a drawing tool is pressed on the canvas, and the drawing is then clipped to the selection at its new place.
The selection may reach beyond the bitmap: painting there grows the bitmap as before.

### Not done

- The ellipse and the rectangle's own options (Shift for a square, from the centre) came with version 19; the fill's options came with version 20.
- Thick brushes and a brush shape (the pixel-perfect rule is for one-cell strokes).
- Dragging a handle of a curve again after the third drag, and a curve with more than two control points.

### Tests

- `tests/test_raster.c`: 51 checks (Bresenham end points, order and symmetry for 169 directions, the constrained angles, the L rule, random walks, the Bezier curve's connectivity, no gaps, no doubled cells and no L corners on 300 random curves; in `build.bat test`).
- `117_line_shift_undo`, `118_pencil_shift_click`, `119_pixel_perfect`, `120_curve` (three drags, Enter, Esc at every stage, Ctrl+Z, a tool change, Shift and Ctrl, the right button, growth), `121_clip_to_selection`, `122_line_curve_locked`.
- Play probes: `assert epp 0|1` (Pixel-perfect on), `assert ecurve 0|1|2` (the curve's stage); `mousedown` and `mouseup` take the word `right` (and `middle`).
- `123_shots_v17` (not in `run_all.bat`) takes the screenshots of the bar at 1400 x 900 and at the narrow sizes.

## Version 18: the tool strip

The tool buttons leave the glyph view's bar and stand in a vertical strip at the left edge of the canvas area, as in common pixel editors.
The top bar keeps Back, the glyph navigation, Fit, the zoom, Metrics and Layers; the second row keeps the options (INK, the padlock, Pixel-perfect or the selection mode, MOVE, ADVANCE).

### The strip

- It lies between the window's left edge and the canvas, from the bottom of the bar to the top of the status line; the canvas is that much narrower (the editor's cell mapping, Fit and the advance handle use the canvas rectangle, so they follow).
- The buttons are the square icon buttons of the bar (`TH_ROW_H`, 28 px) with 2 px between them and 6 px of padding; the strip is 40 px wide with one column and 72 px with two.
- The active tool is lit as before; the tooltip is "Name  (Key)", for example "Pencil  (B)"; the script names are the same as in the bar (`@Pencil`, `@Line`, `@Curve`, `tool.N`).
- `draw_strip` lays the buttons out from `kTool[]`: group by group (a gap of 8 px between groups), inside a group in table order, rows with `hidden` have no button.
  The groups are 0 draw (Pencil, Eraser), 1 shapes (Line, Rectangle, Curve; since version 19 Line, Curve, Rectangle, Ellipse), 2 fill, 3 selection (Marquee, Lasso, Magic Wand, Move).
- One column while all the buttons fit the strip's height, otherwise the buttons wrap into a second column (and a third, if need be) as soon as the next one would not fit.
  With ten tools one column needs about 330 px, so two columns appear only when the strip is shorter (a window under about 560 px high, or a tall preview).
- A later tool (the ellipse, say) is a row in `kTool[]` with its group: the strip needs no other change.

### The bar after the change

- The narrow-window rules of versions 16 and 17 are gone: the bar no longer drops the glyph's name, tightens its gaps or hides the Line / Curve control when the window is narrow, because the nine tools no longer take room in row 1.
  At the application's smallest size (1110 x 600) the second row shows Pixel-perfect, or Set, Add, Sub, And with 8-way, and the MOVE and ADVANCE controls.
- The second row still checks that its controls fit before it draws them, which only a font with a big palette (up to 37 swatches) can now fail; the keys work then.
- The Line / Curve segmented control is gone (the Curve has its button), so the script names `@linemode.0` and `@linemode.1` are gone; scripts click `@Line` and `@Curve`.

### Tests

- `124_tool_strip`: one column at 1400 x 900, two columns at 1110 x 480, a click on a button of the strip chooses the tool, the canvas still draws after the resize.
- Play: `assert etoolcols 1|2` (the strip's columns); `resize` can now go below the application's minimum size (it clears the minimum first).
- `117_line_shift_undo` and `122_line_curve_locked` use the Line and Curve buttons instead of the control.

## Version 19: rectangle and ellipse

The Rectangle gets its modifiers and a Filled option, and the Ellipse is a new tool.
The behaviour is modelled on common pixel editors; the code is pxfe's own (the textbook midpoint ellipse in a bounding box).

### Dragging

- Both tools take the bounding box: the press cell is one corner, the cell under the mouse at the release is the other.
- Shift makes the box a square (a circle for the ellipse): both sides become the longer one, in the direction of the drag.
- Ctrl draws from the centre: the press cell is the middle of the box and the far corner is mirrored through it, as for the Line.
  Shift and Ctrl together make a square from the centre.
- The modifiers count while the button is down: the preview follows them from frame to frame and the release uses those that are held at that moment.
- Esc cancels the drag; nothing is drawn and no undo step is taken.
- The preview is drawn with the ink (the right button previews with `.`), and the glyph is unchanged until the release.
- The release is one undo step.
  The drawing is clipped to the selection like the other drawing tools, grows the bitmap, and is refused when the font is locked.
- A click is one cell; a box one cell wide or high is a line.
- The right button draws with `.`.

The Rectangle keeps its key R (some other editors use U).
The Ellipse is O, because E is the Eraser.
Shift at the press of the key does not choose a filled variant; Filled is a toggle in the bar (see below), so that one rule covers both tools and the mouse.

### The Filled option

- The second row shows one toggle, `Filled` (script name `@Filled`), for the Rectangle and the Ellipse; the Pixel-perfect toggle is shown for the Pencil, the Line and the Curve as before.
- Off, a tool draws the outline; on, it draws the outline and the inside with the ink.
- It is a property of the two tools together, kept in `pxfe.ini` (`[app] filled`) like Pixel-perfect, and `Editor_Filled` and `Editor_SetFilled` are its API.
- The earlier rule of version 17 that Shift filled the Rectangle is gone: Shift is the square now.

### The raster

`src/raster.c` has three new functions (no raylib; `tests/test_raster.c` tests them):

- `ras_box(sx, sy, hx, hy, square, center, box)`: the normalized box of a drag with the two modifiers.
- `ras_rect(out, x0, y0, x1, y1, filled)`: the cells of a rectangle, its outline or all of them.
- `ras_ellipse(out, x0, y0, x1, y1, filled)`: the cells of the ellipse that fills the box.

The ellipse is the midpoint (Bresenham type) ellipse in a bounding box:

- The four mirrored quadrants are stepped from the middle of the sides towards the top and the bottom, with the error terms in integers, so the ring is symmetric in both axes by construction, for even and odd widths and heights alike.
- The cells are sorted and joined: the mirrors meet on the axes, and no cell is in the list twice.
- A flat or narrow ellipse stops early, so the loop finishes the tip; the ring then always touches the box on all four sides.
- The pixel-perfect rule is applied to the set, not to a path: a ring cell with exactly two ring neighbours, each a straight step away along different axes, is the middle of an L and is dropped.
  The two neighbours touch diagonally, so the ring stays whole and one cell thick.
- A filled ellipse fills each row of that ring from its first to its last cell, so the filled shape has the outline of the ring and no holes.
- A box one cell wide or high is a line, and a box two cells wide or high is a solid band (there is no room for a ring).
- Very narrow ellipses (a short side of 3 or 4 cells with a long side of twice that or more) end in a two-cell cap; from a short side of 5 and a ratio under 3 to 1 every ring cell has exactly two neighbours.

The rectangle outline is the box's border; its filled form is the whole box.
A drag over 4096 cells in a side is as large as a glyph's bitmap can be; the cells are a list, built for each frame of the preview.

### The strip

- The strip shows the shape group as Line, Curve, Rectangle, Ellipse.
  The table `kTool[]` keeps its rows and so the numbers of the tools that the scripts use (Curve is 9, Ellipse is 10); a second array, `kStrip[]`, is the order of the buttons, and `draw_strip` lays them out from it.
- A tool joins the strip by a row in `kTool[]` and a place in `kStrip[]`.
- Ellipse has its own icon `TH_IC_ELLIPSE` (an oval outline).

### Tests

- `tests/test_raster.c` (78 checks in all): `ras_box`, `ras_rect`, and for every box from 1 x 1 to 12 x 12 the ellipse's symmetry, no cell twice, a single 8-connected piece, the four sides touched, no L corner, a line for 1 cell and a band for 2, the filled shape (it holds the ring and every row is one run); a 21 x 21 circle is within a cell of the ideal radius; 200 random boxes up to 60 x 60.
- `125_rectangle`: the outline, square, from the centre, both, the modifiers counted at the release, Esc, the click and the line, Filled, the right button, growth.
- `126_ellipse`: key O and the button (E stays the Eraser), the ring of 5 x 5, 8 x 6 and 5 x 3, circle, from the centre, preview, line and band, Filled (21 and 23 cells), the right button, growth.
- `127_shape_clip`: the rectangle and the ellipse, outlined and filled, are clipped to the selection.
- `128_shape_locked`: a locked font refuses both tools, the buttons are off.
- `129_strip_order`: the strip's order (`assert etoolslot<N> T`, the tool number of the N-th button) and the buttons by name, in a tall and a short window.
- `06_editor` and `121_clip_to_selection` use the Filled toggle instead of Shift.
- Play: `assert efilled 0|1`, `assert etoolslot<N> T`.

## Version 20: fill, eyedropper, polygon and contour

The last tool family of the glyph view: the paint bucket gets its options and the clip, the eyedropper and two shape tools (polygon and contour) are new.
The behaviour is modelled on common pixel editors; the code is pxfe's own (a textbook flood fill on a character grid, a textbook even-odd scanline fill).

### The paint bucket (Fill)

- Key F, the strip's Fill button (as before; the tool number stays 4).
- The region is the glyph box as the wand sees it: the advance across the font box (ascent plus descent), widened to the bitmap where that reaches out of the box.
  Empty space can be filled, and the bitmap grows to hold what is filled; a cell outside the region is not part of it.
- The second row shows two toggles for the Fill:
  - `Contiguous` (script name `@Contiguous`, on by default; `assert econtig 0|1`): on, the fill takes the cells joined to the clicked one that hold the same pixel character; off, it takes every cell of the region that holds that character.
  - `8-way` (`@8-way`): the wand's own switch, shared with it, so one setting serves both tools; off, cells join by their sides, on also by their corners.
- The selection is a wall and a limit (since version 20; version 17 left the Fill unclipped): only selected cells are filled, the fill does not pass through unselected cells, and a click outside the selection fills nothing.
  Without a selection nothing limits it.
- A click on a cell that already holds the ink changes nothing and takes no undo step; otherwise the fill is one undo step.
- The left button fills with the ink, the right button with `.` (it clears the character that was clicked, contiguous or global).
- A locked font refuses it.

### The eyedropper

- Key I, the strip's Eyedropper button (tool number 11, in the fill group beside the Fill).
- A click makes the pixel character under the cursor the ink.
  A cell outside the bitmap is empty space, `.`.
  `.` is an ink of every font (the last swatch of the INK palette is the eraser ink), so picking empty space sets the ink to `.`, and the Pencil then erases; a character that the font's palette does not hold is not taken.
- pxfe has one ink and no second one for the eraser, so the right button picks exactly as the left one does.
- Alt held at the press turns the Pencil, Line, Curve, Rectangle, Ellipse, Polygon, Contour or Fill into the eyedropper for that click: the ink is picked, nothing is drawn, and the tool stays what it was (so there is nothing to switch back; the temporary pick lasts the click).
  The Eraser and the selection tools are not included (Alt subtracts in the selection tools).
  Where a reference layer other than the glyph itself is selected, Alt+drag still moves that layer, as before: the layer wins.
- Picking changes neither the glyph nor the undo stack.
- A locked font refuses the tool like the others.

### The polygon

- Key D (free: Ctrl+D is Deselect and is read only with Ctrl), the strip's Polygon button (tool number 12, in the shape group after the Ellipse).
- A click adds a vertex, and a line follows the mouse from the last vertex (the preview, drawn with the ink; the right button previews and draws `.`).
  Nothing changes in the glyph until the polygon is closed.
- It is closed by Enter, by a click on the last vertex again (a double click) or by a click on the first vertex; Esc throws it away and Backspace takes the last vertex back (Backspace is Delete elsewhere, but with a polygon being made it belongs to the polygon).
  Choosing another tool, leaving the glyph, the padlock and `Editor_Flush` close it; Ctrl+Z throws it away (as for the Curve).
- Shift makes each new edge a clean angle from the last vertex, as for the Line (the slopes 0, 1:4, 1:2, 1:1, 2:1, 4:1, 1:0).
- The outline is the line raster between the vertices and from the last back to the first (sharp corners: the vertices stay).
  With the bar's Filled the inside is added by the even-odd rule: a cell is inside when its centre is, so a self-crossing polygon has holes where it crosses itself, and a pentagram's middle stays empty.
  A single vertex is one cell, two are a line, collinear vertices are a line.
- One undo step; the drawing is clipped to the selection, grows the bitmap, and is refused when the font is locked.

### The contour

- Key C (Ctrl+C is Copy and needs Ctrl), the strip's Contour button (tool number 13).
- A freehand drag: the path of the mouse is the contour (the cells between two mouse positions are joined by the line raster), and on the release the path is closed, from where it ended to where it began, and drawn.
  It is a lasso that paints.
- The outline is pixel-perfect: the corner cell of every L of the path is dropped, as for the Pencil with Pixel-perfect on.
  With Filled the inside is added by the even-odd rule, so a path that crosses itself is filled like the polygon's.
- The preview is drawn while the button is down (closed and filled as it will be); Esc cancels the drag.
  A click is one cell and a straight drag is a line.
- The right button draws `.`.
- One undo step; clipped to the selection, grows the bitmap, refused when the font is locked.

### The Filled toggle

The second row shows `Filled` for the Rectangle, the Ellipse, the Polygon and the Contour: the one option covers the four, is kept in `pxfe.ini` (`[app] filled`) and its API is `Editor_Filled` and `Editor_SetFilled` as in version 19.
The Pixel-perfect toggle stays with the Pencil, the Line and the Curve; the contour is always pixel-perfect.

### The raster

`src/raster.c` has two new functions (no raylib; `tests/test_raster.c` tests them):

- `ras_flood(out, pix, rx, ry, rw, rh, sx, sy, contiguous, diagonals, clip)`: the cells of a paint-bucket fill in the region of `rw * rh` characters whose top-left cell is `(rx, ry)`.
  Contiguous is an explicit stack (each cell is pushed once) in four or eight directions; not contiguous marks every cell with the seed's character.
  The optional `clip` mask is a wall (a cell with 0 is neither filled nor passed through), and a seed on such a cell or outside the region fills nothing.
  The cells come in row order.
- `ras_polygon(out, v, n, closed, filled)`: the outline is `ras_line` between consecutive vertices (and from the last to the first when closed and `n >= 3`); a filled closed polygon adds the cells whose centres the even-odd rule finds inside, row by row from the sorted edge crossings, and returns the union in row order without duplicates.

In the editor, `region_pix` builds the character grid of the glyph region once for the wand (`sel_wand_at`) and the Fill (`fill_at`), so both see the same region.
`fill_at` replaces the older `flood_fill`.

### The strip

- The strip reads Pencil, Eraser; Line, Curve, Rectangle, Ellipse, Polygon, Contour; Fill, Eyedropper; Marquee, Lasso, Magic Wand, Move: fourteen buttons in four groups.
  `kTool[]` keeps the tool numbers (Eyedropper 11, Polygon 12, Contour 13) and `kStrip[]` is the order.
- A column holds buttons while they fit the strip's height.
  A group that does not fit the rest of a column starts the next one, so a group is not split while it fits one; a group taller than a column still wraps button by button.
- At the smallest window (1110 x 600) the strip is about 364 px high and fourteen buttons need about 454 px, so there are two columns: the drawing, shape and fill groups in the first (ten buttons) and the selection group in the second.
  At 1110 x 520 it is two columns as well; at 1110 x 480 three.
  In a window 900 px high it is one column.
- Icons `TH_IC_PICK` (a dropper), `TH_IC_POLY` (a polygon with its corners marked) and `TH_IC_CONTOUR` (a wobbly loop).

### The shortcuts of the glyph view

| Key | What it does |
| --- | --- |
| B | Pencil |
| E | Eraser |
| L | Line |
| Shift+L | Curve (also its button) |
| R | Rectangle |
| O | Ellipse |
| D | Polygon |
| C | Contour |
| F | Paint bucket (Fill) |
| I | Eyedropper |
| M, Q, W, V | Marquee, Lasso, Magic Wand, Move |
| 0 to 9 | pick that ink when the font's palette has it |
| Alt at the click | with the Pencil, Line, Curve, Rectangle, Ellipse, Polygon, Contour or Fill: pick the ink under the cursor |
| Right button | draws with `.` (the fill, the shapes, the polygon and the contour too) |
| Shift | the Line and the Curve's end points, the Polygon's edges: clean angles; the Rectangle and Ellipse: a square or circle; the Pencil: a line from the last cell |
| Ctrl | the Line, Curve, Rectangle and Ellipse: draw from the centre |
| Enter | draws the curve or polygon being made; drops floating pixels |
| Esc | cancels a drag, the polygon or the curve being made, the float, the selection, then closes the glyph view |
| Backspace | the polygon being made: its last vertex goes; otherwise clears the selection |
| Delete | clears the selection |
| Ctrl+A, Ctrl+D, Ctrl+Shift+I | select all, deselect, invert |
| Ctrl+C, Ctrl+X, Ctrl+V | copy, cut, paste |
| Ctrl+Z, Ctrl+Y | undo, redo (a float, a curve or a polygon being made is thrown away first) |
| Arrow keys | move the selection by 1 cell, Shift+arrow by 8; with no selection Shift+arrow moves the bitmap |
| PgUp, PgDn | the previous and the next glyph |

### Tests

- `tests/test_raster.c` (110 checks in all): the flood fill (contiguous and global, 4 and 8 directions, a diagonal wall, the clip as a wall and a limit, a seed outside the region or the clip, an empty region such as the growth region) and the polygon (a triangle, a rectangle, a concave U, a bow tie and a pentagram by the even-odd rule against a naive ray count, a point, two points, collinear and repeated vertices, a sliver, negative coordinates).
- `130_fill`: the options, empty space, contiguous and global, 4- and 8-way, the right button, a click that changes nothing, growth, undo.
- `131_fill_clip`: the selection is a wall and a limit, a click outside fills nothing, the right button and the global fill are clipped, two rooms.
- `132_eyedropper` (font 03696f65, two colours): the key and the button, left and right, `.`, Alt with every drawing tool, the tool stays, no undo step.
- `133_polygon`: closing by Enter, by the first vertex and by a double click, Backspace, Esc, Ctrl+Z, another tool, Shift, Filled, a bow tie, the right button, the clip, growth.
- `134_contour`: the outline and Filled, the right button, Esc, a click and a straight drag, a path that crosses itself, the clip, growth.
- `135_tools_locked`: a locked font refuses the four tools; the padlock draws a half-made polygon.
- `136_strip_v20`: the strip at 1110 x 600 (two columns), 1110 x 520 (two), 1110 x 480 (three) and 1400 x 900 (one), every button by name.
- Changed: `129_strip_order` (the new order), `124_tool_strip` and `129_strip_order` use 1110 x 520 for their short window, `121_clip_to_selection` (the Fill is clipped).
- Play: `assert epoly N` (vertices of the polygon being made), `assert econtig 0|1`.
