// Feeds a firmware-update WAV through the real bootloader decoder
// (stm_audio_bootloader QPSK demodulator + packet decoder, same settings as
// monsoon/bootloader/bootloader.cc) and checks the bytes it would flash
// against the original .bin.
//
//   host/build/bootloader_sim build/monsoon/monsoon_hifi.wav build/monsoon/monsoon.bin

#include <cstdio>
#include <cstring>
#include <vector>

#include "stm_audio_bootloader/qpsk/demodulator.h"
#include "stm_audio_bootloader/qpsk/packet_decoder.h"

using namespace stm_audio_bootloader;

static std::vector<uint8_t> ReadFile(const char* path) {
  std::vector<uint8_t> d;
  FILE* f = fopen(path, "rb");
  if (!f) return d;
  uint8_t buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) d.insert(d.end(), buf, buf + n);
  fclose(f);
  return d;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s update.wav firmware.bin\n", argv[0]);
    return 2;
  }
  std::vector<uint8_t> wav = ReadFile(argv[1]);
  std::vector<uint8_t> bin = ReadFile(argv[2]);
  if (wav.size() < 44 || bin.empty()) { fprintf(stderr, "read error\n"); return 2; }
  const int16_t* samples = reinterpret_cast<const int16_t*>(&wav[44]);
  size_t num_samples = (wav.size() - 44) / 2;

  const double kSampleRate = 48000.0, kModulationRate = 6000.0, kBitRate = 12000.0;
  const size_t kBlockSize = 16384;
  static PacketDecoder decoder;
  static Demodulator demodulator;
  decoder.Init(20000);
  demodulator.Init(kModulationRate / kSampleRate * 4294967296.0,
                   kSampleRate / kModulationRate, 2.0 * kSampleRate / kBitRate);
  demodulator.SyncCarrier(true);
  decoder.Reset();

  std::vector<uint8_t> flashed;
  std::vector<uint8_t> block(kBlockSize);
  size_t packet_index = 0, discard = 8000;
  bool done = false, error = false;
  for (size_t i = 0; i < num_samples && !done && !error; i += 32) {
    for (size_t k = i; k < i + 32 && k < num_samples; ++k) {
      if (discard) { --discard; continue; }
      demodulator.PushSample((samples[k] >> 4) + 2048);
    }
    if (demodulator.state() == DEMODULATOR_STATE_OVERFLOW) { error = true; break; }
    demodulator.ProcessAtLeast(32);
    while (demodulator.available() && !done && !error) {
      PacketDecoderState s = decoder.ProcessSymbol(demodulator.NextSymbol());
      if (s == PACKET_DECODER_STATE_OK) {
        memcpy(&block[(packet_index % (kBlockSize / kPacketSize)) * kPacketSize],
               decoder.packet_data(), kPacketSize);
        ++packet_index;
        if (packet_index % (kBlockSize / kPacketSize) == 0) {
          // On the module the CPU stalls while flash is erased/programmed,
          // so the audio interrupt drops those samples. Typical F4 times:
          // 16 KB sector erase 0.25 s, 64 KB 0.55 s, 128 KB 1.1 s, plus
          // ~65 ms to program 16 KB.
          uint32_t address = 0x08008000 + flashed.size();
          double stall = 0.065;
          if (address == 0x08008000 || address == 0x0800C000) stall += 0.25;
          if (address == 0x08010000) stall += 0.55;
          if (address >= 0x08020000 && (address & 0x1FFFF) == 0) stall += 1.1;
          i += size_t(stall * 48000) & ~size_t(31);
          flashed.insert(flashed.end(), block.begin(), block.end());
          decoder.Reset();
          demodulator.SyncCarrier(false);
        } else {
          decoder.Reset();
          demodulator.SyncDecision();
        }
      } else if (s == PACKET_DECODER_STATE_ERROR_SYNC ||
                 s == PACKET_DECODER_STATE_ERROR_CRC) {
        fprintf(stderr, "decoder error %d after %zu packets\n", s, packet_index);
        error = true;
      } else if (s == PACKET_DECODER_STATE_END_OF_TRANSMISSION) {
        done = true;
      }
    }
  }
  bool match = flashed.size() >= bin.size() &&
      memcmp(flashed.data(), bin.data(), bin.size()) == 0;
  printf("decoded %zu packets, %zu bytes flashed, end-of-transmission %s, %s\n",
         packet_index, flashed.size(), done ? "seen" : "NOT seen",
         match ? "image matches the .bin" : "IMAGE MISMATCH");
  return (match && done && !error) ? 0 : 1;
}
