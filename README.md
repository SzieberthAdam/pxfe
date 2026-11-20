# pxfe

A pixel font editor.

pxfe browses and edits the pixel fonts of a [pxfdb](https://github.com/SzieberthAdam/pxfdb) database: typefaces, fonts, glyph bitmaps and colour palettes.
The formats are specified in the pxfdb repository, in its [spec](https://github.com/SzieberthAdam/pxfdb/tree/main/spec) folder.

It is written in C99 with raylib 6 and SQLite 3.

## What it does

- Loads a `.pxfdb` file into memory, works there, and saves it back in one step.
- Imports a folder of text files (`pxtf/`, `pxfm/`, `pxgl/`, `palette/`) into a new database, and exports a database into such a folder.
- Reads and writes pxfp and pxtp packages, to carry a font or a typeface between databases.
- Browses the library by typeface, font and glyph, and finds glyph usage and similar fonts and typefaces.
- Edits a font's metrics and glyphs, and draws glyphs with pencil, line, curve, shape, fill, selection and palette tools.
- Draws fonts of several colours, with per-glyph palettes and a colour picker.
- Imports outline and bitmap font files, and finds a font in a picture of it.

## Building

pxfe builds on Windows today, with the w64devkit GCC bundled with raylib in `C:\raylib`.

```
build.bat          builds pxfe.exe
build.bat debug    builds it with a console and debug info
build.bat test     builds and runs the unit tests
build.bat e2e      builds the end-to-end test binary for tests\e2e\run_all.bat
```

## Running

```
pxfe.exe [<file.pxfdb> | <folder>] [--font <hash>] [--typeface <hash>] [--unlock] [--import-to <file.pxfdb>]
```

A `.pxfdb` file is loaded, and a folder is imported into a new database.
With no argument, the last database is loaded.

## Documents

- [DESIGN.md](DESIGN.md) describes the design, by topic.
- [HANDOFF.md](HANDOFF.md) says where each topic lives in the code and how it is tested.
- [tests/e2e/README.md](tests/e2e/README.md) describes the scripts of the end-to-end tests.

## Fonts

The user interface uses Noto Sans (SIL Open Font License 1.1) and GNU Unifont (SIL OFL 1.1, or the GNU GPL 2 or later with the font embedding exception), in [assets/fonts](assets/fonts).
