// Files compiled into the binary (the port's own art), registered with the VFS at start-up.
#include "assets/vfs.h"

#ifndef VB_DATA_DIR
#error "VB_DATA_DIR must name the folder with the embedded files"
#endif
__asm__(".section .rodata\n"
        ".balign 16\n"
        ".global vb_embedded_menu_buttons\nvb_embedded_menu_buttons:\n.incbin \"" VB_DATA_DIR "/menu_buttons.tga\"\n"
        ".global vb_embedded_menu_buttons_end\nvb_embedded_menu_buttons_end:\n.byte 0\n"
        ".previous\n");
extern "C" const unsigned char vb_embedded_menu_buttons[], vb_embedded_menu_buttons_end[];

void registerEmbeddedFiles() {
    vb::vfs::addEmbedded("embedded:/menu_buttons.tga", vb_embedded_menu_buttons, (size_t)(vb_embedded_menu_buttons_end - vb_embedded_menu_buttons));
}
