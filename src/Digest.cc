#include "ccb/sipm/Digest.hh"

#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace ccb::sipm {
namespace {

constexpr std::array<std::uint32_t, 64> kSha256K = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

std::uint32_t RotR(std::uint32_t value, unsigned amount) {
  return (value >> amount) | (value << (32U - amount));
}

void AppendU64LE(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (unsigned i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>((value >> (8U * i)) & 0xffU));
  }
}

void AppendCanonicalF64LE(std::vector<std::uint8_t>& out, double value) {
  static_assert(sizeof(double) == sizeof(std::uint64_t),
                "ccb-sipm canonical digest requires binary64-sized double");
  static_assert(std::numeric_limits<double>::is_iec559,
                "ccb-sipm canonical digest requires IEC 559 / IEEE-754 double");
  if (!std::isfinite(value)) {
    throw std::invalid_argument(
        "canonical digest input contains non-finite double");
  }
  if (value == 0.0) value = 0.0;
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  AppendU64LE(out, bits);
}

void AppendSchema(std::vector<std::uint8_t>& out, const char* schema) {
  while (*schema != '\0') {
    out.push_back(static_cast<std::uint8_t>(*schema));
    ++schema;
  }
  out.push_back(0U);
}

std::string PrefixSha256(const std::vector<std::uint8_t>& bytes) {
  return std::string("sha256:") + Sha256Hex(bytes);
}

}  // namespace

std::string Sha256Hex(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() > (std::numeric_limits<std::uint64_t>::max() / 8U)) {
    throw std::length_error("SHA-256 input too large");
  }
  std::vector<std::uint8_t> padded = bytes;
  const std::uint64_t bit_length =
      static_cast<std::uint64_t>(bytes.size()) * 8U;
  padded.push_back(0x80U);
  while ((padded.size() % 64U) != 56U) padded.push_back(0U);
  for (int shift = 56; shift >= 0; shift -= 8) {
    padded.push_back(
        static_cast<std::uint8_t>((bit_length >> shift) & 0xffU));
  }

  std::array<std::uint32_t, 8> h = {
      0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
  };

  for (std::size_t offset = 0; offset < padded.size(); offset += 64U) {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
      const std::size_t j = offset + 4U * i;
      w[i] = (static_cast<std::uint32_t>(padded[j]) << 24U) |
             (static_cast<std::uint32_t>(padded[j + 1]) << 16U) |
             (static_cast<std::uint32_t>(padded[j + 2]) << 8U) |
             static_cast<std::uint32_t>(padded[j + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
      const std::uint32_t s0 = RotR(w[i - 15], 7) ^ RotR(w[i - 15], 18) ^
                               (w[i - 15] >> 3U);
      const std::uint32_t s1 = RotR(w[i - 2], 17) ^ RotR(w[i - 2], 19) ^
                               (w[i - 2] >> 10U);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = h[0];
    std::uint32_t b = h[1];
    std::uint32_t c = h[2];
    std::uint32_t d = h[3];
    std::uint32_t e = h[4];
    std::uint32_t f = h[5];
    std::uint32_t g = h[6];
    std::uint32_t hh = h[7];

    for (std::size_t i = 0; i < 64; ++i) {
      const std::uint32_t s1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
      const std::uint32_t ch = (e & f) ^ ((~e) & g);
      const std::uint32_t temp1 = hh + s1 + ch + kSha256K[i] + w[i];
      const std::uint32_t s0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = s0 + maj;
      hh = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
  }

  std::ostringstream os;
  os << std::hex << std::setfill('0');
  for (std::uint32_t word : h) os << std::setw(8) << word;
  return os.str();
}

std::string CanonicalMeasuredImpulseSampleHash(
    const std::vector<double>& time_ns,
    const std::vector<double>& amplitude) {
  if (time_ns.size() != amplitude.size()) {
    throw std::invalid_argument("impulse sample digest length mismatch");
  }
  std::vector<std::uint8_t> bytes;
  AppendSchema(bytes, "CCB_SIPM_IMPULSE_SAMPLES_V1");
  AppendU64LE(bytes, static_cast<std::uint64_t>(time_ns.size()));
  for (std::size_t i = 0; i < time_ns.size(); ++i) {
    AppendCanonicalF64LE(bytes, time_ns[i]);
    AppendCanonicalF64LE(bytes, amplitude[i]);
  }
  return PrefixSha256(bytes);
}

std::string CanonicalEffectiveKernelHash(
    double sample_dt_ns,
    const std::vector<double>& kernel) {
  if (!(sample_dt_ns > 0.0) || !std::isfinite(sample_dt_ns)) {
    throw std::invalid_argument(
        "effective-kernel digest requires finite positive dt");
  }
  std::vector<std::uint8_t> bytes;
  AppendSchema(bytes, "CCB_SIPM_EFFECTIVE_KERNEL_V1");
  AppendCanonicalF64LE(bytes, sample_dt_ns);
  AppendU64LE(bytes, static_cast<std::uint64_t>(kernel.size()));
  for (double value : kernel) AppendCanonicalF64LE(bytes, value);
  return PrefixSha256(bytes);
}

}  // namespace ccb::sipm
