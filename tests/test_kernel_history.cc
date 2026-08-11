#include "ccb/sipm/Config.hh"
#include "ccb/sipm/ResponseSimulator.hh"
#include "ccb/sipm/Types.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

int failures = 0;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

ccb::sipm::ModelConfig KernelConfig() {
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
  c.pulse_rise_ns = 1.0;
  c.pulse_decay_ns = 25.0;
  c.shaper_integrator_stages = 1;
  c.validate();
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

double RawGenericImpulse(double t_ns, const ccb::sipm::ModelConfig& c) {
  return std::exp(-t_ns / c.pulse_decay_ns) -
         std::exp(-t_ns / c.pulse_rise_ns);
}

}  // namespace

int main() {
  constexpr std::uint64_t run_seed = 0x6b65726e656cULL;

  {
    const auto c = KernelConfig();
    const ccb::sipm::ResponseSimulator sim(c);
    const auto r = sim.simulate({Hit(c.history_start_ns)}, run_seed, 1);

    Require(r.avalanches.size() == 1,
            "history-boundary photon must produce one avalanche");
    Require(r.waveform.signal_pe.size() == 11,
            "recorded waveform length must remain window-defined");

    const std::size_t output_samples = r.waveform.signal_pe.size();
    const std::size_t prehistory_samples = static_cast<std::size_t>(
        std::ceil((c.window_start_ns - c.history_start_ns) / c.sample_dt_ns));
    const std::size_t kernel_samples = output_samples + prehistory_samples;

    double peak = 0.0;
    for (std::size_t k = 0; k < kernel_samples; ++k) {
      peak = std::max(
          peak, RawGenericImpulse(static_cast<double>(k) * c.sample_dt_ns, c));
    }
    Require(peak > 0.0, "reference kernel peak must be positive");

    for (std::size_t i = 0; i < output_samples; ++i) {
      const double age_ns =
          c.window_start_ns + static_cast<double>(i) * c.sample_dt_ns -
          c.history_start_ns;
      const double expected = RawGenericImpulse(age_ns, c) / peak;
      Require(std::abs(r.waveform.signal_pe[i] - expected) < 1.0e-12,
              "history avalanche tail must close against analytic kernel");
    }
    Require(r.waveform.signal_pe.front() > 0.0,
            "earliest recorded sample must contain the old-avalanche tail");
    Require(r.waveform.signal_pe.back() > 0.0,
            "latest recorded sample must not be truncated by waveform length");
  }

  {
    auto c = KernelConfig();
    c.history_start_ns = c.window_start_ns;
    c.validate();
    const ccb::sipm::ResponseSimulator sim(c);
    const auto r = sim.simulate({Hit(c.window_start_ns)}, run_seed, 2);
    Require(r.avalanches.size() == 1,
            "collapsed-history control must produce one avalanche");

    double peak = 0.0;
    for (std::size_t k = 0; k < r.waveform.signal_pe.size(); ++k) {
      peak = std::max(
          peak, RawGenericImpulse(static_cast<double>(k) * c.sample_dt_ns, c));
    }
    for (std::size_t i = 0; i < r.waveform.signal_pe.size(); ++i) {
      const double expected =
          RawGenericImpulse(static_cast<double>(i) * c.sample_dt_ns, c) / peak;
      Require(std::abs(r.waveform.signal_pe[i] - expected) < 1.0e-12,
              "collapsed-history convolution must retain legacy grid semantics");
    }
  }

  {
    auto c = KernelConfig();
    c.measured_impulse_t_ns = {0.0, 5.0, 10.0};
    c.measured_impulse_amplitude = {0.0, 1.0, 0.0};
    c.validate();
    const ccb::sipm::ResponseSimulator sim(c);
    const auto r = sim.simulate({Hit(c.history_start_ns)}, run_seed, 3);
    Require(r.avalanches.size() == 1,
            "finite-support control must produce one avalanche");
    for (double value : r.waveform.signal_pe) {
      Require(std::abs(value) < 1.0e-12,
              "kernel extension must not create measured-impulse support");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " kernel-history test(s) failed\n";
    return 1;
  }

  std::cout << "kernel-history coverage OK\n";
  return 0;
}
