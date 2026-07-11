// gfwx_roundtrip_test.cpp — golden known-answer test for kalcutter/gfwx.
//
// Encodes a deterministic synthetic image with GFWX::compress at QualityMax (lossless), decodes it
// back with GFWX::decompress, and asserts BYTE-EXACT pixel recovery. This is a real behavioral
// oracle: a no-op / exit(0) patch, or any change that corrupts the compress↔decompress contract,
// cannot pass. Built with NORMAL flags by build.sh; only RUN by mayhem/test.sh.
//
// It also re-decodes the header alone (imageData==nullptr) and checks the recovered geometry, and
// asserts the encoded stream begins with the 'GFWX' magic — so the test pins both the container
// format and the lossless math.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "gfwx.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", (msg)); ++failures; } \
    else         { std::fprintf(stderr, "ok:   %s\n", (msg)); } \
} while (0)

int main()
{
    const int sizex = 37, sizey = 23, channels = 3, layers = 1;
    const size_t pixels = static_cast<size_t>(sizex) * sizey * channels * layers;

    // Deterministic source image (a reproducible gradient + checker pattern).
    std::vector<uint8_t> src(pixels);
    for (int y = 0; y < sizey; ++y)
        for (int x = 0; x < sizex; ++x)
            for (int c = 0; c < channels; ++c)
                src[(static_cast<size_t>(y) * sizex + x) * channels + c] =
                    static_cast<uint8_t>((x * 7 + y * 13 + c * 53 + ((x ^ y) & 1) * 96) & 0xFF);

    // --- Encode at QualityMax (100% lossless). ---
    GFWX::Header header(sizex, sizey, layers, channels, GFWX::BitDepthAuto,
                        GFWX::QualityMax, /*chromaScale*/8, GFWX::BlockDefault,
                        GFWX::FilterLinear, GFWX::QuantizationScalar,
                        GFWX::EncoderFast, GFWX::IntentRGB);

    std::vector<uint8_t> buffer(pixels * 2 + 1024);   // generous output buffer
    ptrdiff_t encoded = GFWX::compress<uint8_t*>(src.data(), header, buffer.data(), buffer.size(),
                                                 /*channelTransform*/nullptr,
                                                 /*metaData*/nullptr, /*metaDataSizeInWords*/0);
    CHECK(encoded > 0, "compress returned a positive byte count");
    if (encoded <= 0) { std::fprintf(stderr, "compress failed (%ld)\n", (long)encoded); return 1; }

    CHECK(buffer[0] == 'G' && buffer[1] == 'F' && buffer[2] == 'W' && buffer[3] == 'X',
          "encoded stream starts with GFWX magic");

    // --- Header-only decode: geometry must round-trip. ---
    GFWX::Header hdr;
    ptrdiff_t hr = GFWX::decompress<uint8_t*>(nullptr, hdr, buffer.data(), encoded, 0, false);
    CHECK(hr == GFWX::ResultOk, "header-only decompress returns ResultOk");
    CHECK(hdr.sizex == sizex && hdr.sizey == sizey, "decoded header geometry matches");
    CHECK(hdr.channels == channels && hdr.layers == layers, "decoded header channels/layers match");

    // --- Full decode: pixels must be byte-exact (lossless). ---
    std::vector<uint8_t> out(pixels, 0);
    ptrdiff_t dr = GFWX::decompress<uint8_t*>(out.data(), hdr, buffer.data(), encoded, 0, false);
    CHECK(dr == GFWX::ResultOk, "full decompress returns ResultOk");

    bool exact = (out == src);
    CHECK(exact, "lossless round-trip recovers every pixel byte-exactly");
    if (!exact)
    {
        size_t diffs = 0, first = pixels;
        for (size_t i = 0; i < pixels; ++i)
            if (out[i] != src[i]) { if (first == pixels) first = i; ++diffs; }
        std::fprintf(stderr, "  %zu/%zu pixels differ (first at index %zu: got %u want %u)\n",
                     diffs, pixels, first, out[first], src[first]);
    }

    if (failures) { std::fprintf(stderr, "%d check(s) FAILED\n", failures); return 1; }
    std::fprintf(stderr, "all checks passed\n");
    return 0;
}
