// Copyright 2017 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Settings storage.

#ifndef STAGES_SETTINGS_H_
#define STAGES_SETTINGS_H_

#include "stmlib/stmlib.h"
#include "stmlib/system/storage.h"

#include "stages/io_buffer.h"
#include "stages/modes.h"
#include "stages/dobbel.h"

namespace stages {

struct ChannelCalibrationData {
  float adc_offset;
  float adc_scale;
  float dac_offset;
  float dac_scale;

  inline uint16_t dac_code(float level) const {
    int32_t value = level * dac_scale + dac_offset;
    CONSTRAIN(value, 0, 65531);
    return static_cast<uint16_t>(value);
  }
};

struct PersistentData {
  ChannelCalibrationData channel_calibration_data[kNumChannels];
  uint8_t padding[16];

  enum { tag = 0x494C4143 };  // CALI
};

// Segment configuration is 8 bits:
//  - b00000011 (0x03) -> segment type bits
//  - b00000100 (0x04) -> segment loop bit
//  - b01110000 (0x70) -> ouroboros waveshape (8 values)
//
// New:
//  - b00001000 (0x08) -> bipolar bit
//  - b10000000 (0x80) -> alt gate behavior (reset for LFOs)
//
// Other new segment properties occupy the first 8 bits:
//  - b00000011 (0x0300) (8)  ->  stages range
//  - b00001100 (0x0600) (10) ->  ouroboros range
//  - b00110000 (0x0c00) (12) ->  quantization scale
//  - b01000000 (0x4000) (14) ->  envelope follower segment (single module),
//                                or Sync LFO with type bits 01
//  - b10000000 (0x8000) (15) ->  linked to the next segment (single module)
//

const uint16_t kFollowBit = 0x4000;
// The follow bit with type bits 01 is a Sync LFO segment (a follower always
// has type bits 00). Range bits 0x0300 hold its phase offset.
const uint16_t kSyncLfoType = 0x0001;

inline bool is_follower_config(uint16_t c) {
  return (c & kFollowBit) && (c & 0x03) == 0;
}
inline bool is_sync_lfo_config(uint16_t c) {
  return (c & kFollowBit) && (c & 0x03) == kSyncLfoType;
}
const uint16_t kLinkBit = 0x8000;

#define is_bipolar(seg_config) seg_config & 0x08

struct State {
  uint16_t segment_configuration[kNumChannels];
  uint8_t color_blind;
  uint8_t multimode;
  enum { tag = 0x54415453 };  // STAT
};

// The last two 2 KB pages of the 256 KB flash. The audio bootloader only
// erases the pages it writes, so these survive firmware updates as long as the
// application stays below kDobbelStorageStart (checked by tools/dobbel_check.sh).
const uint32_t kDobbelStorageStart = 0x0803F000;
const uint32_t kDobbelStorageEnd = 0x08040000;

class Settings {
 public:
  Settings() { }
  ~Settings() { }

  bool Init();

  void SavePersistentData();
  void SaveState();

  inline ChannelCalibrationData* mutable_calibration_data(int channel) {
    return &persistent_data_.channel_calibration_data[channel];
  }

  inline const ChannelCalibrationData& calibration_data(int channel) const {
    return persistent_data_.channel_calibration_data[channel];
  }

  inline State* mutable_state() {
    return &state_;
  }

  inline const State& state() const {
    return state_;
  }

  inline bool in_seg_gen_mode() const {
    return state_.multimode == MULTI_MODE_STAGES
      || state_.multimode == MULTI_MODE_STAGES_ADVANCED
      || state_.multimode == MULTI_MODE_STAGES_SLOW_LFO;
  }

  inline bool in_dobbel_mode() const {
    return state_.multimode == MULTI_MODE_DOBBEL;
  }

  // fletchfoorth's own settings live in their own flash pages (the last 4 KB),
  // so the State chunk above never changes size: a size change would format
  // the settings and start the factory test.
  inline const dobbel::Config& dobbel() const { return dobbel_; }
  inline dobbel::Config* mutable_dobbel() { return &dobbel_; }
  void SaveDobbel();
  // Flash writes so far. The chip can't run code while it writes flash, so
  // the audio misses a block or two around every save.
  uint32_t saves() const { return saves_; }

  inline bool in_ouroboros_mode() const {
    return state_.multimode == MULTI_MODE_OUROBOROS
      || state_.multimode == MULTI_MODE_OUROBOROS_ALTERNATE;
  }


  inline uint16_t dac_code(int index, float level) const {
    return calibration_data(index).dac_code(level);
  }

 private:
  volatile uint32_t saves_ = 0;
  PersistentData persistent_data_;
  State state_;

  stmlib::ChunkStorage<
      0x08004000,
      0x08008000,
      PersistentData,
      State> chunk_storage_;

  struct DobbelMarker {
    uint32_t magic;
    enum { tag = 0x4B52414D };  // MARK
  };
  DobbelMarker dobbel_marker_;
  dobbel::Config dobbel_;
  stmlib::ChunkStorage<
      kDobbelStorageStart,
      kDobbelStorageEnd,
      DobbelMarker,
      dobbel::Config> dobbel_storage_;

  DISALLOW_COPY_AND_ASSIGN(Settings);
};

}  // namespace stages

#endif  // STAGES_SETTINGS_H_
