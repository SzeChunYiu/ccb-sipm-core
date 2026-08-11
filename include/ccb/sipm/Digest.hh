#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ccb::sipm {

// FIPS-180-4 SHA-256 over the exact supplied bytes, returned as 64 lowercase
// hexadecimal characters with no prefix.
std::string Sha256Hex(const std::vector<std::uint8_t>& bytes);

// Canonical numerical payload identity for sampled impulse pairs. This binds
// the in-memory (time_ns, amplitude) values, not the bytes of any external
// calibration file from which those values may have been parsed.
std::string CanonicalMeasuredImpulseSampleHash(
    const std::vector<double>& time_ns,
    const std::vector<double>& amplitude);

// Canonical identity of the actual runtime impulse kernel after resampling and
// normalization. sample_dt_ns and kernel length are part of the contract.
std::string CanonicalEffectiveKernelHash(
    double sample_dt_ns,
    const std::vector<double>& kernel);

}  // namespace ccb::sipm
