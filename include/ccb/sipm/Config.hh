#pragma once

#include <cstddef>
#include <vector>

namespace ccb::sipm {

struct PdePoint {
  double wavelength_nm = 0.0;
  double pde = 0.0;
};

struct ModelConfig {
  int sensor_id = 0;

  int cells_x = 60;
  int cells_y = 60;
  double active_width_mm = 3.0;
  double active_height_mm = 3.0;

  std::vector<PdePoint> pde_curve;
  double pde_scale = 1.0;
  double coupling_efficiency = 1.0;
  bool pde_includes_fill_factor = true;

  double recovery_time_ns = 30.0;
  double dead_time_ns = 0.0;
  double gain_mean_pe = 1.0;
  double gain_sigma_fraction = 0.05;
  double sptr_sigma_ns = 0.10;

  bool enable_dark_counts = true;
  double dark_count_rate_hz = 500000.0;

  bool enable_prompt_crosstalk = true;
  double prompt_crosstalk_probability = 0.03;
  double prompt_crosstalk_jitter_ns = 0.02;
  int crosstalk_neighbourhood = 4;

  bool enable_delayed_crosstalk = false;
  double delayed_crosstalk_probability = 0.0;
  double delayed_crosstalk_tau_ns = 20.0;

  bool enable_afterpulsing = true;
  double afterpulse_fast_probability = 0.01;
  double afterpulse_fast_tau_ns = 15.0;
  double afterpulse_slow_probability = 0.005;
  double afterpulse_slow_tau_ns = 80.0;

  double window_start_ns = -20.0;
  double window_end_ns = 250.0;

  bool generate_waveform = true;
  double sample_dt_ns = 0.5;
  double pulse_rise_ns = 1.0;
  double pulse_decay_ns = 25.0;
  double electronics_noise_sigma_pe = 0.03;
  double baseline_adc = 200.0;
  double adc_lsb_pe = 0.01;
  int adc_bits = 12;

  std::size_t max_candidates = 1000000;

  int number_of_cells() const { return cells_x * cells_y; }
  void validate() const;

  static ModelConfig RepresentativeS13360_3050CS();
};

}  // namespace ccb::sipm
