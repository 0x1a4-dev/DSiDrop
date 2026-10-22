# DSiDrop artwork

`brand-source.png` is the unmodified logo supplied by the project owner.
The drop with the handheld pictogram is imported from the left of that image;
the wordmark is excluded. Its proportions and original transparency are kept.

Run `python nds/tools/build_assets.py` from the web project to regenerate:

- `icon.png`: native 32 by 32 indexed preview, with transparent palette index 0.
- `icon.bmp`: 4-bit Windows BMP consumed by `ndstool` for the launcher banner.
- `../include/icon.h`: native icon pixels, dedicated RGB555 icon palette and
  color mapping for the existing 4-bit GUI canvases.

DS banner limits require 32 by 32 pixels and 16 palette entries. The import
fits the source pictogram into 30 by 30 pixels, centers it, then converts it
to 15 opaque colors plus transparency. Opaque black in the handheld remains
distinct from transparent space. Intro sprites use the dedicated icon palette;
the GUI canvas maps it onto its shared violet, white and ink palette.
