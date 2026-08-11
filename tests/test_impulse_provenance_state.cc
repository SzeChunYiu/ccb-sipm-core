#include "ccb/sipm/Config.hh"
#include "ccb/sipm/ResponseSimulator.hh"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

ccb::sipm::ModelConfig BaseConfig() {
  auto c = ccb::sipm::ModelConfig::RepresentativeS13360_3050CS();
  c.enable_dark_counts = false;
  c.enable_prompt_crosstalk = false;
  c.enable_delayed_crosstalk = false;
  c.enable_afterpulsing = false;
  c.electronics_noise_sigma_pe = 0.0;
  c.measured_impulse_t_ns = {0.0, 1.0, 2.0};
  c.measured_impulse_amplitude = {0.0, 1.0, 0.0};
  return c;
}

}  // namespace

int main() {
  // Atomic contract: numerical validity of an arbitrary sampled impulse is not
  // evidence that it is a measured/calibrated electronics response.  Without
  // bound source identity, exact digests and calibration validation, metadata
  // must remain non-authoritative (e.g. CUSTOM_UNVALIDATED), never MEASURED.
  {
    auto c = BaseConfig();
    c.validate();
    const ccb::sipm::ResponseSimulator sim(c);
    const auto md = sim.run_metadata();

    Require(md.electronics.measured_impulse_source_id.empty(),
            "negative control must have no source identity");
    Require(md.electronics.measured_impulse_source_hash.empty(),
            "negative control must have no source digest");
    Require(md.electronics.effective_kernel_hash.empty(),
            "negative control must have no effective-kernel digest");
    Require(md.electronics.impulse_response_status != "MEASURED",
            "synthetic sampled kernel without provenance must not claim MEASURED");
  }

  // A human-readable source label alone is not a content/provenance binding.
  // This hostile fixture blocks promotion based only on a non-empty source id.
  {
    auto c = BaseConfig();
    c.electronics_provenance.measured_impulse_source_id = "synthetic-fixture";
    c.validate();
    const ccb::sipm::ResponseSimulator sim(c);
    const auto md = sim.run_metadata();
    Require(md.electronics.impulse_response_status != "MEASURED",
            "source label without exact digests/validation must not authorize MEASURED");
  }

  if (failures != 0) {
    std::cerr << failures << " impulse-provenance-state test(s) failed\n";
    return 1;
  }

  std::cout << "impulse provenance state contract OK\n";
  return 0;
}
