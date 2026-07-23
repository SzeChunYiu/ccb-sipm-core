#include "ccb/sipm/Config.hh"

#include <cmath>
#include <stdexcept>

namespace ccb::sipm {

namespace {
void RequireProbability(double value, const char* name, bool allow_one = true) {
  const bool valid = std::isfinite(value) && value >= 0.0 &&
                     (allow_one ? value <= 1.0 : value < 1.0);
  if (!valid) {
    throw std::invalid_argument(std::string(name) + " is outside its probability domain");
  }
}
}  // namespace

void ModelConfig::validate() const {
  if (cells_x <= 0 || cells_y <= 0) {
    throw std::invalid_argument("cell dimensions must be positive");
  }
  if (!(active_width_mm > 0.0) || !(active_height_mm > 0.0)) {
    throw std::invalid_argument("active dimensions must be positive");
  }
  if (pde_curve.size() < 2) {
    throw std::invalid_argument("PDE curve requires at least two points");
  }
  for (std::size_t i = 0; i < pde_curve.size(); ++i) {
    if (!std::isfinite(pde_curve[i].wavelength_nm) ||
        !std::isfinite(pde_curve[i].pde)) {
      throw std::invalid_argument("PDE curve contains non-finite values");
    }
    RequireProbability(pde_curve[i].pde, "PDE value");
    if (i > 0 && !(pde_curve[i].wavelength_nm >
                   pde_curve[i - 1].wavelength_nm)) {
      throw std::invalid_argument("PDE wavelengths must be strictly increasing");
    }
  }
  if (!std::isfinite(pde_scale) || pde_scale < 0.0) {
    throw std::invalid_argument("PDE scale must be finite and non-negative");
  }
  RequireProbability(coupling_efficiency, "coupling efficiency");
  if (!(recovery_time_ns > 0.0) || dead_time_ns < 0.0) {
    throw std::invalid_argument("invalid recovery/dead time");
  }
  if (!(gain_mean_pe > 0.0) || gain_sigma_fraction < 0.0 ||
      sptr_sigma_ns < 0.0) {
    throw std::invalid_argument("invalid gain or SPTR parameters");
  }
  if (dark_count_rate_hz < 0.0) {
    throw std::invalid_argument("dark count rate must be non-negative");
  }
  RequireProbability(prompt_crosstalk_probability,
                     "prompt crosstalk probability", false);
  RequireProbability(delayed_crosstalk_probability,
                     "delayed crosstalk probability", false);
  RequireProbability(afterpulse_fast_probability,
                     "fast afterpulse probability", false);
  RequireProbability(afterpulse_slow_probability,
                     "slow afterpulse probability", false);
  if (prompt_crosstalk_jitter_ns < 0.0 ||
      !(delayed_crosstalk_tau_ns > 0.0) ||
      !(afterpulse_fast_tau_ns > 0.0) ||
      !(afterpulse_slow_tau_ns > 0.0)) {
    throw std::invalid_argument("invalid correlated-noise time parameter");
  }
  if (crosstalk_neighbourhood != 4 && crosstalk_neighbourhood != 8) {
    throw std::invalid_argument("crosstalk neighbourhood must be 4 or 8");
  }
  if (!(window_end_ns > window_start_ns)) {
    throw std::invalid_argument("waveform window is empty");
  }
  if (!(sample_dt_ns > 0.0) || !(pulse_rise_ns > 0.0) ||
      !(pulse_decay_ns > pulse_rise_ns)) {
    throw std::invalid_argument("invalid waveform sampling or pulse constants");
  }
  if (electronics_noise_sigma_pe < 0.0 || !(adc_lsb_pe > 0.0) ||
      adc_bits <= 0 || adc_bits > 30) {
    throw std::invalid_argument("invalid electronics/ADC parameters");
  }
  if (max_candidates == 0) {
    throw std::invalid_argument("max_candidates must be positive");
  }
}

ModelConfig ModelConfig::RepresentativeS13360_3050CS() {
  ModelConfig c;
  c.pde_curve = {
      {300.0, 0.06}, {350.0, 0.18}, {400.0, 0.33}, {450.0, 0.40},
      {476.0, 0.38}, {500.0, 0.35}, {550.0, 0.27}, {600.0, 0.18},
      {650.0, 0.11}, {700.0, 0.06},
  };
  return c;
}

}  // namespace ccb::sipm
