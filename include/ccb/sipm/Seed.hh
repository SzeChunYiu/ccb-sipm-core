#pragma once

#include <cstdint>

namespace ccb::sipm {

std::uint64_t SplitMix64(std::uint64_t x);
std::uint64_t MakeEventSeed(std::uint64_t run_seed,
                            std::uint64_t event_id,
                            std::uint64_t sensor_id,
                            std::uint64_t stream_id = 0);

}  // namespace ccb::sipm
