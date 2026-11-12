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
| `src/main.c` | window, app state, views, toolbar with the File menu and Save, Load / Save / Save As / Import / Export flows with their prompts and dialogs, the commit of a duplicate into memory, glyph grid, metrics panel, status bar |
| `src/editor.h/.c` | the glyph editing view |
| `src/play.inc` | the scripted input player of the end-to-end tests (`--play`) |

## Command line

`pxfe.exe [<file.pxfdb> | <folder>] [--font <hash>] [--typeface <hash>] [--duplicate] [--import-to <file.pxfdb>]`.

- A `.pxfdb` file is loaded.
- A folder starts the import flow: the target file is asked for (or given with `--import-to`), the folder is imported, the new database is loaded.
- `--font` and `--typeface` open that font or typeface of the loaded database; `--duplicate` then makes the editable copy.
- With no argument, the last database (`lastPxfdb` in `pxfe.ini` beside the exe) is loaded, else an empty state with "Load Database…" and "Import Folder…".

## Visual direction

The bar is the polished look of FontLab 8: quiet, flat, precise.

- A light neutral theme: window background `#F3F3F4`, panels `#FFFFFF`, hairline separators `#DCDCE0`, text `#1E1E22`, secondary text `#7A7A85`, accent (selection) `#2F6FED`, accent tint `#2F6FED` at 15% alpha.
- One UI font: Segoe UI at 13 px and 11 px, Consolas 12 px for hashes and code points; never scaled bitmap text.
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
- The default glyph (`_DEF`) is a property of the font (the code point of the default glyph, or none), shown as a badge on its cell and settable from the grid's context.
- Kerning pairs are kept and written; they are not edited yet, but deleting a glyph drops its pairs and cloning does not copy them.

## Originals, duplicates and change detection

The files of a library are named by their hashes, and the user wants the originals left as they are.

- A font opened from the library is an original: it opens read-only.
  The grid, the glyph view, selection and zoom all work, but every command that would change it is disabled, and a bar above the grid says "Original — read-only" with a "Duplicate to Edit" button.
- "Duplicate to Edit" (in the browser and in the font view) makes a duplicate: an in-memory working copy of the font (its pxfm and the pxgl glyphs it names) and, when the font came from a container, of the container (pxfc) too.
  The duplicate is editable.
  Its title says "Duplicate of <name>".
- A duplicate is saved as a draft (`.pxfd`) in the drafts folder (`drafts\` beside `pxfe.exe`); the browser lists the drafts in their own tab.
- "Save to Library" of a duplicate writes the new pxgl files, the new pxfm and, for a container duplicate, a new pxfc whose variant names the new pxfm and holds the edited metadata.
  It is disabled while the duplicate's canonical pxfm (and pxfc) hash equals the original's, with the tooltip "Unchanged: same hash as the original".
  No existing file is ever overwritten: content addressing gives every changed element a new name.
- A glyph's pxgl is never changed in place either: editing a glyph gives it a new bitmap hash, and the glyph view shows the original pxgl hash and the new one.
- Change detection is by hash, never by "an edit happened".
  The font is modified exactly when the canonical hash of its current state differs from the hash it was opened with, so an edit that is undone, or a value set to what it was, leaves it unmodified.
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
