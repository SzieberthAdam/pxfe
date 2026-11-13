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
- The tab is read-only for an original.
- "Duplicate Typeface to Edit" (a button at the top of the tab, and in the Typeface view's heading; the same look as the font's Duplicate to Edit) makes an editable copy of the typeface.
  The copy is the usual duplicate document: the font of the selected variant (unchanged) with a copy of the typeface.
  The view stays on the typeface; the heading reads "Editing a copy".
- In a copy the fields and the three switches of the variant of the selected row are editable (Enter or a click elsewhere keeps a field, Escape puts its value back), the boxed hash shows the current hash and the original hash below it.
- The first nav segment shows the family of the copy with the "*" marker while it differs from the original.
- A copy follows the rules of version 5: it cannot be committed while its canonical hash equals the original's; leaving the Typeface view (or any other view), opening a font, Save, Export, Load and quit commit it into memory as a new typeface under its new pxtf hash, replacing its own earlier commit; the original typeface and its fonts stay.
  A typeface edit leaves the font as it was, so a later commit names the same pxfm again: the commit keeps the ownership of what its earlier commit created (`CommitDuplicate`).
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

#### Duplicate to Edit

- Owner: the macros `Th_Duplicate(r, typeface, off, hint)`, `Th_DuplicateW(typeface)` and `Th_DupLabel(typeface)` in `theme.h`: a `Th_Button` with `TH_BTN_PRIMARY`, so the wording and the flag exist once.
- Wording: "Duplicate to Edit" for a font, "Duplicate Typeface to Edit" for a typeface; nothing else is a label of this command (the context menu item uses `Th_DupLabel` too).
- Every command that makes an editable copy is this button: the info bar of the font view, the glyph view's bar, the cards, rows and selection block of the library views, the Typeface view's heading and its font rows, the Typeface tab.
- A read-only notice beside it ("Read-only" in the glyph view's bar) is a separate plain label, not part of the button.
- The glyph view's button keeps the script name `ro_chip`; the others register the label (`@Duplicate_to_Edit`, `@Duplicate_Typeface_to_Edit`).

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
| Duplicate to Edit button | `Th_Duplicate`, `Th_DuplicateW`, `Th_DupLabel` (macros over `Th_Button`) | `theme.h` |
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
