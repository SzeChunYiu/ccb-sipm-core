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
  // Atomic contract: a numerically valid sampled impulse is not evidence that
  // the electronics response is measured/calibrated.  Until a separate
  // content-bound promotion contract exists, sampled custom kernels are
  // explicitly serialized as CUSTOM_UNVALIDATED rather than MEASURED.
  {
    auto c = BaseConfig();
    c.validate();
    const ccb::sipm::ResponseSimulator sim(c);
    const auto md = sim.run_metadata();

    Require(md.impulse_model == "MEASURED",
            "numerical sampled-impulse model family remains selected");
    Require(md.electronics.measured_impulse_source_id.empty(),
            "negative control must have no source identity");
    Require(md.electronics.measured_impulse_source_hash.empty(),
            "negative control must have no source digest");
    Require(md.electronics.effective_kernel_hash.empty(),
            "negative control must have no effective-kernel digest");
    Require(md.electronics.impulse_response_status == "CUSTOM_UNVALIDATED",
            "synthetic sampled kernel without provenance must be quarantined");
  }

  // A human-readable source label is not a content binding.
  {
    auto c = BaseConfig();
    c.electronics_provenance.measured_impulse_source_id = "synthetic-fixture";
    c.validate();
    const ccb::sipm::ResponseSimulator sim(c);
    const auto md = sim.run_metadata();
    Require(md.electronics.impulse_response_status == "CUSTOM_UNVALIDATED",
            "source label alone must not authorize MEASURED");
  }

  // Even strings placed in digest-named fields are not sufficient for
  // promotion: the core has not yet verified that they bind source bytes or
  // the history-complete effective kernel.  This blocks caller assertions from
  // laundering an arbitrary synthetic vector into MEASURED status.
  {
    auto c = BaseConfig();
    c.electronics_provenance.measured_impulse_source_id = "synthetic-fixture";
    c.electronics_provenance.measured_impulse_source_hash =
        "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    c.electronics_provenance.effective_kernel_hash =
        "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    c.validate();
    const ccb::sipm::ResponseSimulator sim(c);
    const auto md = sim.run_metadata();
    Require(md.electronics.impulse_response_status == "CUSTOM_UNVALIDATED",
            "unverified digest strings must not authorize MEASURED");
  }

  if (failures != 0) {
    std::cerr << failures << " impulse-provenance-state test(s) failed\n";
    return 1;
  }

  std::cout << "impulse provenance state contract OK\n";
  return 0;
}
