#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ccb::sipm {

struct PdePoint {
  double wavelength_nm = 0.0;
  double pde = 0.0;
};

struct DeviceProfileProvenance {
  std::string device_name;
  std::string profile_file;
  std::string schema;
  double overvoltage_V = 0.0;
  double temperature_C = 0.0;
  std::string calibration_status;
  std::string primary_source_id;
  std::string primary_source_type;
  std::string primary_source_title;
  std::string primary_source_url;
  std::string primary_source_retrieved_date;
  std::string secondary_source_id;
  std::string secondary_source_type;
  std::string secondary_source_title;
  std::string secondary_source_url;
  std::string secondary_source_retrieved_date;
  std::string covered_parameters;
  std::string warning;
};

struct ElectronicsProvenance {
  std::string impulse_response_status;
  std::string shaper_model;
  int integrator_stages = 1;
  std::string measured_impulse_source_id;
  std::string measured_impulse_source_url;
  std::string measured_impulse_retrieved_date;
  // Exact content/effective-kernel digests when supplied by a validated
  // provenance layer.  The core must not synthesize non-cryptographic
  // placeholders into fields that downstream users may interpret as hashes.
  std::string measured_impulse_source_hash = "";
  std::string effective_kernel_hash = "";
  std::string note;
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
  double history_start_ns = -200.0;

  bool generate_waveform = true;
  double sample_dt_ns = 0.5;

  double pulse_rise_ns = 1.0;
  double pulse_decay_ns = 25.0;
  int shaper_integrator_stages = 1;
  double shaper_extra_stage_tau_ns = 25.0;

  // Generic analytical response, a validated measured response, or an
  // explicitly authorised test-only ideal delta.  Measured vectors reconcile
  // this field to MEASURED during validate().
  mutable std::string impulse_model = "GENERIC_CRRC";
  bool authorising = false;
  std::vector<double> measured_impulse_t_ns;
  std::vector<double> measured_impulse_amplitude;

  double electronics_noise_sigma_pe = 0.03;
  double baseline_adc = 200.0;
  double adc_lsb_pe = 0.01;
  int adc_bits = 12;

  DeviceProfileProvenance device_provenance;
  ElectronicsProvenance electronics_provenance;

  std::size_t max_candidates = 1000000;

  int number_of_cells() const { return cells_x * cells_y; }
  void validate() const;

  static int ApplyEnvironmentOverrides(ModelConfig& config);
  static ModelConfig RepresentativeS13360_3050CS();
};

struct RunMetadata {
  std::string engine = "ccb::sipm::ResponseSimulator";
  std::string engine_version = "0.1.0";
  std::string schema = "ccb-sipm-run-metadata/1";
  DeviceProfileProvenance device;
  ElectronicsProvenance electronics;
  std::vector<PdePoint> pde_curve;
  double recovery_time_ns = 0.0;
  double dark_count_rate_hz = 0.0;
  double prompt_crosstalk_probability = 0.0;
  double delayed_crosstalk_probability = 0.0;
  double afterpulse_fast_probability = 0.0;
  double afterpulse_slow_probability = 0.0;
  int adc_bits = 0;
  double adc_lsb_pe = 0.0;
  double baseline_adc = 0.0;
  double sample_dt_ns = 0.0;
  double window_start_ns = 0.0;
  double window_end_ns = 0.0;
  double history_start_ns = 0.0;
  std::string impulse_model;
  std::string render_json() const;
};

}  // namespace ccb::sipm
