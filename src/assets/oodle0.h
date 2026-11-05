#pragma once
// Granny2 "Oodle0" section decompressor (GR2 section compression type 1), ported from granny2.dll (FUN_1001d060 and helpers).
// Algorithm: adaptive-model arithmetic coder + LZ copies; see GR2_FORMAT_FINDINGS.md.
#include <cstddef>
#include <cstdint>

namespace vb {

// `comp` = the section's compressed bytes (9 header dwords = 3 model parameter sets, then the coded stream).
// stop0/stop1/stop2 = first16bit offset, first8bit offset and expanded size from the section header; `out` has room for stop2 bytes.
// Returns false on a malformed stream (bad match distance, output overrun).
bool oodle0Decompress(const uint8_t* comp, size_t compSize, uint32_t stop0, uint32_t stop1, uint32_t stop2, uint8_t* out);

} // namespace vb
