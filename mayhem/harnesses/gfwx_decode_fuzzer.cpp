// gfwx_decode_fuzzer.cpp — libFuzzer harness for kalcutter/gfwx's GFWX image DECODER.
//
// Fuzzed surface: GFWX::decompress() on attacker-controlled GFWX bytes. The input IS a raw
// GFWX bitstream (magic 'GFWX' = 0x58 0x57 0x46 0x47 little-endian at byte 0). The harness:
//   1) Reads the header only  (imageData == nullptr) to learn sizex/sizey/layers/channels/bitDepth.
//   2) Rejects/clamps absurd geometry so a malformed header can't drive a multi-GB allocation
//      (the codec itself permits sizex/sizey up to 2^30 → header.bufferSize() can be astronomical).
//   3) Allocates the decode buffer and runs the FULL decode (imageData != nullptr) at a couple of
//      downsampling levels, exercising the wavelet unlift / dequantize / color-transform paths.
//
// We pick uint8_t as the element type, matching header.isSigned==0 && bitDepth<=8 inputs; signed or
// >8-bit headers are rejected by decompress() with ErrorTypeMismatch (a clean, expected return) so
// the harness simply stops — no crash, by design. This mirrors how a real 8-bit RGBA decoder client
// would call GFWX. The interesting bug surface (truncation handling, block-size arithmetic, the
// Bits stream over/underflow logic, lift/quantize integer math) is fully reachable on 8-bit inputs.
//
// The codec is a single C++20 header; we instantiate decompress<uint8_t*> here.
#include <cstdint>
#include <cstddef>
#include <vector>

#include "gfwx.h"

// Hard cap on the pixel-buffer we are willing to allocate per input. Keeps the fuzzer fast and
// prevents a malformed header (huge sizex*sizey*channels*layers) from OOM-killing the run. This is
// a harness policy, NOT a codec limit — the codec is still exercised on every header below the cap.
static const size_t kMaxPixels = 1u << 22;   // 4 Mi elements (~4 MB for uint8_t)

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size == 0)
        return 0;

    // --- 1) Header-only probe: imageData == nullptr returns ResultOk (or a negative error). ---
    GFWX::Header header;
    ptrdiff_t r = GFWX::decompress<uint8_t*>(nullptr, header, data, size, 0, false);
    if (r != GFWX::ResultOk)
        return 0;   // malformed / truncated header, or "need N more bytes" — nothing to decode.

    // --- 2) Sanity-clamp geometry before allocating. ---
    if (header.sizex <= 0 || header.sizey <= 0 || header.layers <= 0 || header.channels <= 0)
        return 0;
    // Reject signed / >8-bit headers: decompress<uint8_t*> would return ErrorTypeMismatch anyway,
    // but bailing here avoids a wasted allocation.
    if (header.isSigned != 0 || header.bitDepth > 8)
        return 0;

    size_t bufferSize = header.bufferSize();        // 0 when the multiply would overflow size_t.
    if (bufferSize == 0 || bufferSize > kMaxPixels)
        return 0;

    // --- 3) Full decode at downsampling 0 and 1. ---
    std::vector<uint8_t> image(bufferSize, 0);
    for (int downsampling = 0; downsampling <= 1; ++downsampling)
    {
        GFWX::Header h2 = header;   // decompress may mutate the header; use a fresh copy each pass.
        // 'test' = false → actually reconstruct pixels (exercises unlift/dequantize/transform).
        GFWX::decompress<uint8_t*>(image.data(), h2, data, size, downsampling, false);
    }
    return 0;
}
