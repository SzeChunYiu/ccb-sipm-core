#include "ccb/sipm/Digest.hh"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int failures = 0;
void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

template <typename F>
void RequireThrows(F&& fn, const char* message) {
  try {
    fn();
    Require(false, message);
  } catch (const std::invalid_argument&) {
  }
}
}  // namespace

int main() {
  using ccb::sipm::CanonicalEffectiveKernelHash;
  using ccb::sipm::CanonicalMeasuredImpulseSampleHash;
  using ccb::sipm::Sha256Hex;

  Require(Sha256Hex({}) ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "FIPS SHA-256 empty-string vector mismatch");
  Require(Sha256Hex({'a', 'b', 'c'}) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "FIPS SHA-256 abc vector mismatch");

  const std::vector<double> t = {0.0, 1.0, 2.0};
  const std::vector<double> a = {0.0, 1.0, 0.0};
  const std::string sample_hash = CanonicalMeasuredImpulseSampleHash(t, a);
  Require(sample_hash ==
              "sha256:dbceaf26fdae5b951099d8a76aef391bdc94c53336bbbd0cf63e49fc00a5b094",
          "canonical sampled-impulse digest mismatch");

  std::vector<double> negative_zero_a = {-0.0, 1.0, -0.0};
  Require(CanonicalMeasuredImpulseSampleHash(t, negative_zero_a) == sample_hash,
          "-0 and +0 must collapse to one numerical payload state");

  std::vector<double> tampered_a = a;
  tampered_a[1] = 0.999;
  Require(CanonicalMeasuredImpulseSampleHash(t, tampered_a) != sample_hash,
          "sample digest must change after amplitude tamper");

  Require(CanonicalEffectiveKernelHash(1.0, {0.0, 1.0, 0.0}) ==
              "sha256:aa049b621977903cb9c4cb0423dd1bf6844f59a667c593a906b725531b79e29a",
          "canonical effective-kernel digest mismatch");
  Require(CanonicalEffectiveKernelHash(1.0, {0.0, 1.0, 0.0, 0.0}) ==
              "sha256:d943f8002a50b1f2c83de80aa50495e7511e541563033d2801e6351edb5c08f6",
          "kernel length/history extension must participate in identity");
  Require(CanonicalEffectiveKernelHash(0.5, {0.0, 1.0, 0.0}) !=
              CanonicalEffectiveKernelHash(1.0, {0.0, 1.0, 0.0}),
          "sample spacing must participate in effective-kernel identity");

  RequireThrows(
      [] { CanonicalMeasuredImpulseSampleHash({0.0, 1.0}, {1.0}); },
      "sample digest must reject mismatched vector lengths");
  RequireThrows(
      [] {
        CanonicalMeasuredImpulseSampleHash(
            {0.0, std::numeric_limits<double>::infinity()}, {0.0, 1.0});
      },
      "sample digest must reject non-finite values");
  RequireThrows(
      [] { CanonicalEffectiveKernelHash(0.0, {1.0}); },
      "effective-kernel digest must reject non-positive dt");

  if (failures != 0) {
    std::cerr << failures << " impulse digest test(s) failed\n";
    return 1;
  }
  std::cout << "impulse digest canonicalization contract OK\n";
  return 0;
}
