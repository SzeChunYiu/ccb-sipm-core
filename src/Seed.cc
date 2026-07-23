#include "ccb/sipm/Seed.hh"

namespace ccb::sipm {

std::uint64_t SplitMix64(std::uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27U)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31U);
}

std::uint64_t MakeEventSeed(std::uint64_t run_seed,
                            std::uint64_t event_id,
                            std::uint64_t sensor_id,
                            std::uint64_t stream_id) {
  std::uint64_t x = SplitMix64(run_seed);
  x ^= SplitMix64(event_id + 0x243f6a8885a308d3ULL);
  x ^= SplitMix64(sensor_id + 0x13198a2e03707344ULL);
  x ^= SplitMix64(stream_id + 0xa4093822299f31d0ULL);
  return SplitMix64(x);
}

}  // namespace ccb::sipm
