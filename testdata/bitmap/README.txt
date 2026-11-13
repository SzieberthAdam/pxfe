testdata/bitmap - tiny bitmap font files for tests/test_bmsrc.c (the reader src/bmsrc.c)

All files are made by tests/gen_bitmap_fonts.py (python tests/gen_bitmap_fonts.py; standard library only).
They are hand-made, a few glyphs each, and released into the public domain (CC0 1.0), like the script.
No real font file is copied here: the files of pixelglyphlib/sourcefiles and the images the sheet descriptors name have
unclear or no redistribution licences, so they stay where they are (the reader was checked against them by hand, not by the tests).
The layout of every format follows pixelglyphlib/pixelglyphlib/readers (bdf.py, pcf.py, fnt.py, sheet.py, sbit.py);
the files were also read back with those Python readers to check that both agree.

Shapes: A and B are 5x7 letters, the apostrophe is 2x3 raised 4 rows, p is 4x5 with a descender, "E" is a 3x3 stand-in for e-acute.

font.bdf              BDF, ENCODING by CHARSET_REGISTRY "microsoft" / "437" (code 130 = U+00E9), 6 glyphs, one without encoding
font_lsb.pcf          PCF, uncompressed metrics, LSB byte and bit order, pad 1, ISO8859-1, with accelerators and glyph names
font_msb.pcf          PCF, compressed metrics, MSB byte and bit order, pad 2, ISO10646 (U+263A), sparse two-row encoding table
font_mix.pcf          PCF, MSB bits with LSB bytes, pad 4, scan unit 4 (the scan-unit byte swap), no accelerators, no names
font.fnt              Windows FNT 3.0, proportional, charset 0 (cp1252), 'A'..'D', 'C' without image, 'D' two byte columns wide
fixed_prop_ne.fon     NE executable with two RT_FONT resources: an 8x8 FNT 2.0 in cp437 (codes 0x80..0x82) and the FNT of font.fnt
fixed_prop_pe.fon     the same two fonts in a PE file (resource directory .rsrc, type 8)
sheet.png, sheet.json image sheet: 8 cells of 6x8 in a 4x2 grid, grey PNG, black on white, codes from 65, codepage "ascii", baseline measured
sheet_alpha.png/.json RGBA PNG, white ink on transparent, cells 4x5, codes from 1, "cp437-graphic" (U+263A ...), baseline given
raw.bin, raw.json     raw dump, 8x4 cells, 4 glyphs from code 126, "cp852-graphic"
raw9.json             the same dump read with "ninth_column": [128, 128] (VGA 9-dot mode)
records.bin/.json     "records" sheet (width byte, one byte per pixel, padding to 16), cell 3x4, codes from 32, advance_gap, space_advance
strikes.ttf           sfnt with head, hhea, maxp, hmtx, cmap (format 4), name, EBLC and EBDT, no outlines; 3 strikes:
                        ppem 8, 1 bit: index format 1, image formats 1 (A, B; B has advance 0 so hmtx gives it) and 8 (C = A and B as components)
                        ppem 14, 1 bit: index format 2, image format 5 (bit aligned, constant metrics): A and B doubled
                        ppem 10, 2 bits: index format 3, image format 6 (big metrics): A with coverage 0..3, no line metrics (hhea is scaled)

Other file in this tree: vendor/stb_image.h (public domain or MIT, by Sean Barrett; see vendor/stb_image.LICENSE.txt) is used by src/bmsrc.c
to decode the PNG, GIF, BMP and TGA of image sheets and of CBDT strikes.
