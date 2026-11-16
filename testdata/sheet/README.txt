Pictures of fonts for tests/test_sheet.c (src/sheet.c) and the end-to-end script 104.

font8x8.png, font-20x20-RPG.png, font-1-tds.png, title-font-shmup.png
  From the Superpowers asset packs (github.com/sparklinlabs/superpowers-asset-packs), made at Sparklin Labs by Pixel-boy.
  Licence: CC0 1.0 Universal (public domain dedication); copied unchanged.
  font8x8: a grid of 8 x 8 cells, 15 x 8 of them, from the space, one colour, glyphs that touch their neighbours.
  font-20x20-RPG: a grid of 20 x 20 cells, three colours (fill, highlight, outline); the small letters are capitals.
  font-1-tds: four rows of glyphs of their own widths, from '!', three colours.
  title-font-shmup: A-Z and 0-9 in cells of 42 x 33 whose outlines touch, six colours.

marker8.png, enlarged3.png
  Generated from font8x8.png by tests/gen_sheet_fonts.py (python -I tests/gen_sheet_fonts.py, needs Pillow) and committed.
  marker8: '!' to '~' as a strip with a marker row (single magenta pixels in the top row over the columns between the glyphs).
  enlarged3: the first four rows of font8x8.png with every pixel a block of 3 x 3, on white.
  Licence: CC0 1.0, as their source.
