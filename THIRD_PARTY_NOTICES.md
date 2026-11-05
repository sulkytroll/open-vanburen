# Third-party notices

The MIT license in `LICENSE` covers this repository's code and notes. It does **not** cover the Fallout 3 "Van Buren" tech demo, which is not included
and remains the property of its owners.

* **Built-in text font** (`tools/common/font_data.h`): a bitmap rasterisation of *DejaVu Sans Mono*. DejaVu fonts are based on Bitstream Vera, copyright
  (c) 2003 Bitstream, Inc., with the DejaVu changes in the public domain; see <https://dejavu-fonts.github.io/License.html>.
* **`data/menu_buttons.png` / `.tga`**: the main-menu button picture, made for this project.
* **Libraries** used when building (not included): SDL2 (zlib license), GLEW (modified BSD / MIT), FreeType (FreeType License or GPLv2), OpenAL Soft (LGPL 2).
* **Format documentation**: the `.grp` / `.rht` layouts were first documented by [bsimser/Van-Buren-Explorer](https://github.com/bsimser/Van-Buren-Explorer) (MIT).
* `src/assets/oodle0.cpp` is a reimplementation of the Granny2 "Oodle0" decompressor, written from analysis of the format and of the decompressor's behaviour for
  interoperability and checked byte-for-byte against the original's output. No Granny / RAD Game Tools source code or binaries are included.
