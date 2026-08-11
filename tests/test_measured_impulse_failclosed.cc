#include "ccb/sipm/Config.hh"
#include "ccb/sipm/ResponseSimulator.hh"
#include "ccb/sipm/Types.hh"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
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
  c.cells_x = 1;
  c.cells_y = 1;
  c.active_width_mm = 1.0;
  c.active_height_mm = 1.0;
  c.pde_curve = {{300.0, 1.0}, {700.0, 1.0}};
  c.pde_scale = 1.0;
  c.coupling_efficiency = 1.0;
  c.enable_dark_counts = false;
  c.enable_prompt_crosstalk = false;
  c.enable_delayed_crosstalk = false;
  c.enable_afterpulsing = false;
  c.gain_sigma_fraction = 0.0;
  c.sptr_sigma_ns = 0.0;
  c.electronics_noise_sigma_pe = 0.0;
  c.history_start_ns = -20.0;
  c.window_start_ns = 0.0;
  c.window_end_ns = 10.0;
  c.sample_dt_ns = 1.0;
  return c;
}

ccb::sipm::PhotonArrival Hit(double time_ns) {
  ccb::sipm::PhotonArrival p;
  p.sensor_id = 0;
  p.time_ns = time_ns;
  p.wavelength_nm = 450.0;
  p.x_mm = 0.0;
  p.y_mm = 0.0;
  p.has_local_position = true;
  return p;
}

bool ValidateThrows(ccb::sipm::ModelConfig c) {
  try {
    c.validate();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

}  // namespace

int main() {
  constexpr std::uint64_t seed = 0x1067ULL;

  {
    auto c = BaseConfig();
    c.measured_impulse_t_ns = {0.0, 1.0, 2.0, 3.0, 4.0};
    c.measured_impulse_amplitude = {0.0, 0.5, 1.0, 0.5, 0.0};
    c.validate();
    Require(c.impulse_model == "MEASURED",
            "valid sampled vectors must select the sampled-impulse model family");
    const ccb::sipm::ResponseSimulator sim(c);
    const auto r = sim.simulate({Hit(0.0)}, seed, 1);
    Require(std::abs(r.waveform.signal_pe[2] - 1.0) < 1.0e-12,
            "valid sampled impulse must retain its non-delta peak offset");
    const auto metadata = sim.run_metadata();
    Require(metadata.electronics.impulse_response_status == "CUSTOM_UNVALIDATED",
            "unbound sampled impulse must not claim measured calibration");
    Require(metadata.electronics.measured_impulse_source_hash.empty(),
            "core must not fabricate a source hash placeholder");
    Require(metadata.electronics.effective_kernel_hash.empty(),
            "core must not fabricate an effective-kernel hash placeholder");
  }

  {
    auto c = BaseConfig();
    c.measured_impulse_t_ns = {0.0, 1.0, 2.0};
    c.measured_impulse_amplitude = {0.0, 0.0, 0.0};
    Require(ValidateThrows(c), "all-zero measured impulse must fail closed");
  }

  {
    auto c = BaseConfig();
    c.measured_impulse_t_ns = {0.0, 1.0, 2.0};
    c.measured_impulse_amplitude = {-0.5, -1.0, -0.5};
    Require(ValidateThrows(c), "negative-integral measured impulse must fail closed");
  }

  {
    auto c = BaseConfig();
    c.impulse_model = "IDEAL_DELTA_TEST_ONLY";
    c.authorising = false;
    Require(ValidateThrows(c), "unauthorised ideal delta must fail closed");
    c.authorising = true;
    c.validate();
    const ccb::sipm::ResponseSimulator sim(c);
    const auto r = sim.simulate({Hit(0.0)}, seed, 2);
    Require(std::abs(r.waveform.signal_pe[0] - 1.0) < 1.0e-12,
            "authorised test-only ideal delta must peak at fire sample");
    Require(std::abs(r.waveform.signal_pe[1]) < 1.0e-12,
            "authorised test-only ideal delta must have zero tail");
  }

  // Cross-atom discriminator: the recorded window lasts only 10 ns, but the
  // admitted history makes the elapsed-time kernel domain 30 ns.  Support at
  // 20--25 ns is therefore relevant and must not be rejected merely because it
  // lies beyond the recorded-window duration.
  {
    auto c = BaseConfig();
    c.measured_impulse_t_ns = {20.0, 22.0, 25.0};
    c.measured_impulse_amplitude = {0.0, 1.0, 0.0};
    c.validate();
    const ccb::sipm::ResponseSimulator sim(c);
    const auto r = sim.simulate({Hit(c.history_start_ns)}, seed, 3);
    Require(r.waveform.signal_pe.size() == 11,
            "recorded output length remains window-defined");
    Require(std::abs(r.waveform.signal_pe[2] - 1.0) < 1.0e-12,
            "history-domain measured support must contribute in-window");
  }

  // Support wholly beyond the maximum history-to-output lag is invalid.
  {
    auto c = BaseConfig();
    c.measured_impulse_t_ns = {40.0, 42.0, 45.0};
    c.measured_impulse_amplitude = {0.0, 1.0, 0.0};
    Require(ValidateThrows(c),
            "measured support beyond history-complete kernel must fail closed");
  }

  if (failures != 0) {
    std::cerr << failures << " measured-impulse composition test(s) failed\n";
    return 1;
  }
  std::cout << "measured-impulse/history composition OK\n";
  return 0;
}
