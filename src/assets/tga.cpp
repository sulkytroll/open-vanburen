#include "assets/vfs.h"
#include "assets/tga.h"
#include <cstring>
#include <fstream>

namespace vb {

namespace {
struct Reader {
    std::vector<uint8_t> b;
    size_t p = 0;
    bool u16(uint16_t& v) {
        if (p + 2 > b.size()) return false;
        v = b[p] | b[p+1] << 8; p += 2; return true;
    }
    bool u8(uint8_t& v) { if (p + 1 > b.size()) return false; v = b[p++]; return true; }
};

// Emit one source pixel (r,g,b[,a]) into the RGBA buffer at row/col.
void putPixel(std::vector<uint8_t>& out, int w, int h, int x, int y,
              uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    size_t i = ((size_t)y * w + x) * 4;
    out[i+0]=r; out[i+1]=g; out[i+2]=b; out[i+3]=a;
}
} // namespace

bool loadTga(const std::string& path, Texture& out, std::string& err) {
    vfs::Stream f(path);
    if (!f) { err = "cannot open " + path; return false; }
    Reader rd;
    rd.b.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (rd.b.size() < 18) { err = "too small"; return false; }

    uint8_t idlen, cmaplen, imgtype;
    if (!rd.u8(idlen) || !rd.u8(cmaplen) || !rd.u8(imgtype)) { err="hdr"; return false; }
    rd.p += 2; // cmap origin
    uint16_t cmplen; rd.u16(cmplen);
    uint8_t cmapbpp; rd.u8(cmapbpp);
    rd.p += 4; // x/y origin
    uint16_t w, h;
    if (!rd.u16(w) || !rd.u16(h)) { err="hdr"; return false; }
    uint8_t bpp, attrib;
    if (!rd.u8(bpp) || !rd.u8(attrib)) { err="hdr"; return false; }

    // Skip id field and colormap (we don't support indexed images).
    rd.p += idlen;
    rd.p += (size_t)cmplen * (cmapbpp / 8);

    bool topDown = (attrib & 0x20) != 0;   // bit 5
    size_t bytesPerPx = bpp / 8;
    if (bytesPerPx < 1 || bytesPerPx > 4) { err = "bad bpp " + std::to_string(bpp); return false; }
    if (imgtype != 2 && imgtype != 3 && imgtype != 5 && imgtype != 10 && imgtype != 11 && imgtype != 13) {
        err = "unsupported TGA type " + std::to_string(imgtype); return false;
    }
    bool rle = (imgtype == 10 || imgtype == 11 || imgtype == 13);          // 10 = RLE true colour (the f3_digit_*.tga files)

    out.width = w; out.height = h;
    out.rgba.assign((size_t)w * h * 4, 0);

    // Palette for 8-bit gray / colormap (rare here); handle 8-bit as grayscale.
    size_t pxCount = 0;
    auto emit = [&](const uint8_t* src) {
        int x = (int)pxCount % w;
        int y = topDown ? (int)pxCount / w : h - 1 - (int)pxCount / w;
        uint8_t r,g,b,a;
        switch (bpp) {
            case 8:  r=g=b=src[0]; a=255; break;
            case 16: { uint16_t v=src[0]|src[1]<<8;          // TGA 16-bit is A1R5G5B5
                       r=(v>>10)&31; g=(v>>5)&31; b=v&31;
                       r=(r<<3)|(r>>2); g=(g<<3)|(g>>2); b=(b<<3)|(b>>2); a=(v&0x8000)?255:0; break; }
            case 24: b=src[0]; g=src[1]; r=src[2]; a=255; break;      // TGA stores BGR(A)
            case 32: b=src[0]; g=src[1]; r=src[2]; a=src[3]; break;
        }
        putPixel(out.rgba, w, h, x, y, r, g, b, a);
        ++pxCount;
    };

    if (!rle) {
        for (size_t i = 0; i < (size_t)w*h; ++i) {
            if (rd.p + bytesPerPx > rd.b.size()) { err="truncated"; return false; }
            emit(rd.b.data() + rd.p);
            rd.p += bytesPerPx;
        }
    } else {
        while (pxCount < (size_t)w*h) {
            uint8_t hdr;
            if (!rd.u8(hdr)) { err="truncated RLE"; return false; }
            uint8_t count = (hdr & 0x7F) + 1;
            uint8_t px[4] = {0,0,0,0};
            if (hdr & 0x80) {
                // Run: one pixel repeated `count` times.
                if (rd.p + bytesPerPx > rd.b.size()) { err="truncated RLE"; return false; }
                for (size_t k=0;k<bytesPerPx;++k) px[k]=rd.b[rd.p++];
                for (uint8_t c=0;c<count && pxCount<(size_t)w*h;++c) emit(px);
            } else {
                // Raw block.
                for (uint8_t c=0;c<count && pxCount<(size_t)w*h;++c) {
                    if (rd.p + bytesPerPx > rd.b.size()) { err="truncated RLE"; return false; }
                    for (size_t k=0;k<bytesPerPx;++k) px[k]=rd.b[rd.p++];
                    emit(px);
                }
            }
        }
    }
    return true;
}

} // namespace vb
