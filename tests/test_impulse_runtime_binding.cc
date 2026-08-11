#include "ccb/sipm/Config.hh"
#include "ccb/sipm/ResponseSimulator.hh"
#include "ccb/sipm/Types.hh"

#include <cmath>
#include <cstdint>
#include <iostream>
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

ccb::sipm::ModelConfig BindingConfig(double history_start_ns,
                                     bool generate_waveform = true) {
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
  c.gain_mean_pe = 1.0;
  c.gain_sigma_fraction = 0.0;
  c.sptr_sigma_ns = 0.0;
  c.electronics_noise_sigma_pe = 0.0;
  c.baseline_adc = 0.0;
  c.adc_lsb_pe = 1.0;
  c.window_start_ns = 0.0;
  c.window_end_ns = 2.0;
  c.history_start_ns = history_start_ns;
  c.sample_dt_ns = 1.0;
  c.generate_waveform = generate_waveform;
  c.measured_impulse_t_ns = {0.0, 1.0, 2.0, 3.0};
  c.measured_impulse_amplitude = {0.0, 1.0, 0.0, 0.0};
  c.electronics_provenance.effective_kernel_hash =
      "sha256:caller-supplied-placeholder";
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

void RequireClose(double actual, double expected, const char* message) {
  Require(std::abs(actual - expected) < 1.0e-12, message);
}

}  // namespace

int main() {
  constexpr std::uint64_t run_seed = 0x72756e74696d65ULL;
  const std::string three_sample_hash =
      "sha256:aa049b621977903cb9c4cb0423dd1bf6844f59a667c593a906b725531b79e29a";
  const std::string four_sample_hash =
      "sha256:d943f8002a50b1f2c83de80aa50495e7511e541563033d2801e6351edb5c08f6";

  {
    const auto c = BindingConfig(0.0);
    const ccb::sipm::ResponseSimulator sim(c);
    const auto metadata = sim.run_metadata();
    Require(metadata.electronics.impulse_response_status ==
                "CUSTOM_UNVALIDATED",
            "runtime binding must not authorize MEASURED provenance");
    Require(metadata.electronics.effective_kernel_hash == three_sample_hash,
            "collapsed-history metadata must hash exact 3-sample kernel");
    Require(metadata.electronics.effective_kernel_hash !=
                c.electronics_provenance.effective_kernel_hash,
            "caller-supplied effective-kernel hash must be overwritten");

    const auto result = sim.simulate({Hit(0.0)}, run_seed, 1);
    Require(result.avalanches.size() == 1,
            "collapsed-history control must produce one avalanche");
    Require(result.waveform.signal_pe.size() == 3,
            "collapsed-history waveform must have three output samples");
    RequireClose(result.waveform.signal_pe[0], 0.0,
                 "waveform sample 0 must equal cached kernel sample 0");
    RequireClose(result.waveform.signal_pe[1], 1.0,
                 "waveform sample 1 must equal cached kernel sample 1");
    RequireClose(result.waveform.signal_pe[2], 0.0,
                 "waveform sample 2 must equal cached kernel sample 2");
  }

  {
    const auto c = BindingConfig(-1.0);
    const ccb::sipm::ResponseSimulator sim(c);
    const auto metadata = sim.run_metadata();
    Require(metadata.electronics.effective_kernel_hash == four_sample_hash,
            "history-complete metadata must hash exact 4-sample kernel");

    const auto result = sim.simulate({Hit(-1.0)}, run_seed, 2);
    Require(result.avalanches.size() == 1,
            "history-boundary control must produce one avalanche");
    Require(result.waveform.signal_pe.size() == 3,
            "recorded waveform length must remain window-defined");
    RequireClose(result.waveform.signal_pe[0], 1.0,
                 "pre-window avalanche must consume kernel age 1");
    RequireClose(result.waveform.signal_pe[1], 0.0,
                 "pre-window avalanche must consume kernel age 2");
    RequireClose(result.waveform.signal_pe[2], 0.0,
                 "pre-window avalanche must consume kernel age 3");
  }

  {
    auto c = BindingConfig(-1.0);
    c.measured_impulse_amplitude = {0.0, 2.0, 0.0, 0.0};
    const ccb::sipm::ResponseSimulator sim(c);
    Require(sim.run_metadata().electronics.effective_kernel_hash ==
                four_sample_hash,
            "positive global source-amplitude scale must collapse after peak normalization");
  }

  {
    auto c = BindingConfig(-1.0);
    c.measured_impulse_amplitude = {0.0, 1.0, 0.25, 0.0};
    const ccb::sipm::ResponseSimulator sim(c);
    Require(sim.run_metadata().electronics.effective_kernel_hash !=
                four_sample_hash,
            "shape-changing source mutation must change effective-kernel identity");
  }

  {
    const auto c = BindingConfig(0.0, false);
    const ccb::sipm::ResponseSimulator sim(c);
    const auto metadata = sim.run_metadata();
    Require(metadata.electronics.effective_kernel_hash.empty(),
            "no-waveform mode must not advertise an unconsumed runtime-kernel hash");
    const auto result = sim.simulate({Hit(0.0)}, run_seed, 3);
    Require(result.waveform.signal_pe.empty(),
            "no-waveform control must not synthesize a waveform");
  }

  if (failures != 0) {
    std::cerr << failures << " impulse runtime-binding test(s) failed\n";
    return 1;
  }

  std::cout << "impulse runtime-kernel provenance binding OK\n";
  return 0;
}
