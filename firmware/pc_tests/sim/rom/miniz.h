// Same API as the ESP32 ROM's tinfl, implemented with zlib for the simulation.
#pragma once
#include <zlib.h>
#include <cstddef>
#include <cstdint>
typedef struct { int m_state; } tinfl_decompressor;
typedef enum { TINFL_STATUS_FAILED = -1, TINFL_STATUS_DONE = 0 } tinfl_status;
#define TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF 4
#define tinfl_init(r) do { (r)->m_state = 0; } while (0)
inline tinfl_status tinfl_decompress(tinfl_decompressor*, const uint8_t* in, size_t* inSize, uint8_t*, uint8_t* out, size_t* outSize, int) {
  z_stream z{}; inflateInit2(&z, -15);
  z.next_in = (Bytef*)in; z.avail_in = *inSize; z.next_out = out; z.avail_out = *outSize;
  int r = inflate(&z, Z_FINISH); *inSize = z.total_in; *outSize = z.total_out; inflateEnd(&z);
  return r == Z_STREAM_END ? TINFL_STATUS_DONE : TINFL_STATUS_FAILED;
}
