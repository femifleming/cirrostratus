// Renders audio through one mode for listening.
//
//   host/build/render --mode grains --in music --secs 12 --out out.wav \
//       --set position=0.3 --set density=0.7 --freeze-at 4 --trig-every 0.5
//
// --in accepts a .wav path, or: music, noise, sine:<hz>, silence.
// --sweep <param> sweeps a knob 0..1 over the render; --ramp <param>=a:b
// moves it from a to b. --quality 0..5 sets the buffer quality (48k 16-bit ..
// 16k 8-bit).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "host/harness.h"
#include "host/wav.h"

using namespace host;

static float* Param(Controls* c, const std::string& name) {
  if (name == "position") return &c->position;
  if (name == "size") return &c->size;
  if (name == "density") return &c->density;
  if (name == "texture") return &c->texture;
  if (name == "pitch") return &c->pitch;
  if (name == "blend") return &c->blend;
  if (name == "spread") return &c->bal;
  if (name == "feedback") return &c->fbk;
  if (name == "reverb") return &c->rvb;
  return NULL;
}

int main(int argc, char** argv) {
  std::string mode_name = "grains", in_name = "music", out_name = "out.wav";
  std::string sweep;
  float secs = 10.0f, freeze_at = -1.0f, unfreeze_at = -1.0f, trig_every = 0.0f;
  int option = 0, quality = 0;
  std::vector<std::pair<std::string, std::pair<float, float>>> ramps;
  Controls base = DefaultControls();
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() { return std::string(i + 1 < argc ? argv[++i] : ""); };
    if (a == "--mode") mode_name = next();
    else if (a == "--in") in_name = next();
    else if (a == "--out") out_name = next();
    else if (a == "--secs") secs = std::atof(next().c_str());
    else if (a == "--option") option = std::atoi(next().c_str());
    else if (a == "--freeze-at") freeze_at = std::atof(next().c_str());
    else if (a == "--unfreeze-at") unfreeze_at = std::atof(next().c_str());
    else if (a == "--trig-every") trig_every = std::atof(next().c_str());
    else if (a == "--sweep") sweep = next();
    else if (a == "--quality") quality = std::atoi(next().c_str());
    else if (a == "--ramp") {
      std::string kv = next();
      size_t eq = kv.find('='), co = kv.find(':');
      if (eq == std::string::npos || co == std::string::npos ||
          !Param(&base, kv.substr(0, eq))) {
        std::fprintf(stderr, "bad ramp %s\n", kv.c_str());
        return 1;
      }
      ramps.push_back({kv.substr(0, eq),
                       {float(std::atof(kv.substr(eq + 1, co - eq - 1).c_str())),
                        float(std::atof(kv.substr(co + 1).c_str()))}});
    }
    else if (a == "--set") {
      std::string kv = next();
      size_t eq = kv.find('=');
      float* p = Param(&base, kv.substr(0, eq));
      if (!p) { std::fprintf(stderr, "unknown param %s\n", kv.c_str()); return 1; }
      *p = std::atof(kv.substr(eq + 1).c_str());
    } else {
      std::fprintf(stderr, "unknown arg %s\n", a.c_str());
      return 1;
    }
  }

  Module module;
  int mode = -1;
  for (int m = 0; m < monsoon::MODE_LAST; ++m) {
    if (mode_name == monsoon::Engine::mode_name(monsoon::ModeId(m))) mode = m;
  }
  if (mode < 0) { std::fprintf(stderr, "unknown mode %s\n", mode_name.c_str()); return 1; }
  module.engine().set_buffer_quality(uint8_t(quality));
  module.SetMode(monsoon::ModeId(mode), option);

  std::vector<FloatFrame> in;
  if (in_name == "music") in = Music(secs);
  else if (in_name == "noise") in = Noise(secs);
  else if (in_name == "silence") in = Silence(secs);
  else if (in_name.rfind("sine:", 0) == 0) in = Sine(std::atof(in_name.c_str() + 5), secs);
  else {
    int sr;
    if (!ReadWav(in_name, &in, &sr)) { std::fprintf(stderr, "can't read %s\n", in_name.c_str()); return 1; }
    in.resize(std::min(in.size(), size_t(secs * 48000)) & ~size_t(31));
  }
  const size_t blocks = in.size() / monsoon::kBlockSize;
  const float block_s = monsoon::kBlockSize / 48000.0f;
  size_t trig_blocks = trig_every > 0 ? size_t(trig_every / block_s) : 0;
  auto out = module.Render(in, [&](size_t b) {
    Controls c = base;
    float t = b * block_s;
    if (!sweep.empty()) *Param(&c, sweep) = float(b) / blocks;
    for (auto& r : ramps) {
      const float x = float(b) / blocks;
      *Param(&c, r.first) = r.second.first + (r.second.second - r.second.first) * x;
    }
    c.freeze = freeze_at >= 0 && t >= freeze_at && (unfreeze_at < 0 || t < unfreeze_at);
    c.trigger = trig_blocks && (b % trig_blocks) == 0;
    c.gate = trig_blocks && (b % trig_blocks) < trig_blocks / 2;
    c.option = uint8_t(option);
    return c;
  });
  Stats s = Measure(out);
  std::printf("%s: peak %.1f dBFS, rms %.1f dBFS, arena %zu/%zu bytes\n",
              mode_name.c_str(), Db(s.peak), Db(s.rms),
              module.engine().arena_used(), module.engine().arena_capacity());
  WriteWav(out_name, out, 48000);
  return 0;
}
