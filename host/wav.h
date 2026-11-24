// Minimal WAV reader/writer for the host harness (16/24-bit PCM, float).

#ifndef MONSOON_HOST_WAV_H_
#define MONSOON_HOST_WAV_H_

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "monsoon/dsp/core.h"

namespace host {

inline bool ReadWav(const std::string& path, std::vector<monsoon::FloatFrame>* out,
                    int* sample_rate) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  std::vector<uint8_t> d;
  uint8_t buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) d.insert(d.end(), buf, buf + n);
  fclose(f);
  if (d.size() < 12 || memcmp(&d[0], "RIFF", 4) || memcmp(&d[8], "WAVE", 4)) return false;
  size_t p = 12;
  int channels = 0, bits = 0, format = 0;
  *sample_rate = 0;
  while (p + 8 <= d.size()) {
    uint32_t size;
    memcpy(&size, &d[p + 4], 4);
    if (!memcmp(&d[p], "fmt ", 4)) {
      uint16_t fmt, ch, bps;
      uint32_t sr;
      memcpy(&fmt, &d[p + 8], 2);
      memcpy(&ch, &d[p + 10], 2);
      memcpy(&sr, &d[p + 12], 4);
      memcpy(&bps, &d[p + 22], 2);
      format = fmt; channels = ch; *sample_rate = sr; bits = bps;
      if (fmt == 0xFFFE) format = d[p + 8 + 24];  // extensible: subformat
    } else if (!memcmp(&d[p], "data", 4)) {
      size_t bytes_per = bits / 8;
      size_t frames = size / (bytes_per * channels);
      out->resize(frames);
      for (size_t i = 0; i < frames; ++i) {
        float v[2] = {0, 0};
        for (int c = 0; c < channels && c < 2; ++c) {
          const uint8_t* s = &d[p + 8 + (i * channels + c) * bytes_per];
          if (format == 3 && bits == 32) {
            memcpy(&v[c], s, 4);
          } else if (bits == 16) {
            int16_t x; memcpy(&x, s, 2); v[c] = x / 32768.0f;
          } else if (bits == 24) {
            int32_t x = (s[0] << 8) | (s[1] << 16) | (s[2] << 24);
            v[c] = (x >> 8) / 8388608.0f;
          }
        }
        if (channels == 1) v[1] = v[0];
        (*out)[i].l = v[0];
        (*out)[i].r = v[1];
      }
      return true;
    }
    p += 8 + size + (size & 1);
  }
  return false;
}

inline bool WriteWav(const std::string& path,
                     const std::vector<monsoon::FloatFrame>& in, int sample_rate) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  uint32_t data_bytes = static_cast<uint32_t>(in.size() * 2 * 3);
  auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); w32(36 + data_bytes); fwrite("WAVE", 1, 4, f);
  fwrite("fmt ", 1, 4, f); w32(16); w16(1); w16(2); w32(sample_rate);
  w32(sample_rate * 6); w16(6); w16(24);
  fwrite("data", 1, 4, f); w32(data_bytes);
  for (const auto& fr : in) {
    for (float v : {fr.l, fr.r}) {
      if (v > 1.0f) v = 1.0f;
      if (v < -1.0f) v = -1.0f;
      int32_t x = static_cast<int32_t>(v * 8388607.0f);
      uint8_t b[3] = { uint8_t(x), uint8_t(x >> 8), uint8_t(x >> 16) };
      fwrite(b, 1, 3, f);
    }
  }
  fclose(f);
  return true;
}

}  // namespace host

#endif  // MONSOON_HOST_WAV_H_
