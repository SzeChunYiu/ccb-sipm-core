#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ccb::sipm {

struct PdePoint {
  double wavelength_nm = 0.0;
  double pde = 0.0;
};

// Provenance for the device profile used to build a ModelConfig.  All
// representative / manufacturer-typical parameters share this single
// provenance block because they come from the same two Hamamatsu sources at
// the same operating point.  The block is deliberately explicit that the
// values are NOT a calibration against a measured device: device-specific
// validation (V-DEV-*, V-ELEC-*) is an open operator-bench item.
struct DeviceProfileProvenance {
  // Device identity
  std::string device_name;           // e.g. "Hamamatsu S13360-3050CS"
  std::string profile_file;          // e.g. "config/s13360_3050cs_REPRESENTATIVE.json"
  std::string schema;                // e.g. "ccb-sipm-device-profile/1"

  // Operating point the parameters are stated at
  double overvoltage_V = 0.0;
  double temperature_C = 0.0;

  // Calibration status.  The string literal is intentionally explicit:
  // MANUFACTURER_REPRESENTATIVE_NOT_CALIBRATED means the numbers are the
  // vendor's representative/typical values, not a fit to a serialised device.
  std::string calibration_status;    // "MANUFACTURER_REPRESENTATIVE_NOT_CALIBRATED"

  // Primary source for device dimensions / cells / pitch / DCR / PDE surface
  std::string primary_source_id;     // "SRC-HAMA-001"
  std::string primary_source_type;   // "MANUFACTURER"
  std::string primary_source_title;
  std::string primary_source_url;
  std::string primary_source_retrieved_date;  // ISO-8601, e.g. "2026-07-23"

  // Secondary source for recovery / correlated-noise definitions
  std::string secondary_source_id;   // "SRC-HAMA-002"
  std::string secondary_source_type; // "MANUFACTURER_GUIDE"
  std::string secondary_source_title;
  std::string secondary_source_url;
  std::string secondary_source_retrieved_date;

  // Comma-separated list of ModelConfig parameters covered by this provenance
  // (PDE curve + the correlated/uncorrelated-noise defaults).
  std::string covered_parameters;

  // Free-text caveat inherited from the device-profile JSON.
  std::string warning;
};

// Provenance for the front-end / electronics impulse response.  Until a
// measured single-PE impulse is supplied the status stays
// ASSUMPTION_GENERIC_CRRC_NOT_MEASURED; supplying measured_impulse_* on the
// ModelConfig flips this to MEASURED with the matching source fields filled.
struct ElectronicsProvenance {
  std::string impulse_response_status;  // "ASSUMPTION_GENERIC_CRRC_NOT_MEASURED"
  std::string shaper_model;             // "CR-RC(-RC) semi-gaussian, configurable stages"
  int integrator_stages = 1;            // 1 = CR-RC bi-exponential, 2 = CR-RC-RC
  std::string measured_impulse_source_id;        // "" when generic
  std::string measured_impulse_source_url;       // "" when generic
  std::string measured_impulse_retrieved_date;   // "" when generic
  std::string note;  // documents the peak-normalisation + the measured hook
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

  // --- Front-end / electronics (generic, configurable) ---------------------
  // The microcell waveform is a delta-train of amplitude-normalised Geiger
  // pulses.  The front-end convolves that train with a causal impulse
  // response h(t), adds electronics noise, and quantises on an ADC.
  //
  // Two impulse sources are supported, selected by whether the
  // measured_impulse_* vectors are empty:
  //
  //   * Generic (default): an analytical CR-RC(-RC) semi-gaussian shaper.
  //       - stages == 1: classic bi-exponential pulse
  //             h(t) = (exp(-t/pulse_decay_ns) - exp(-t/pulse_rise_ns))
  //         This is a CR differentiator (tau = pulse_decay_ns) followed by an
  //         RC integrator (tau = pulse_rise_ns), the standard SiPM pulse
  //         shape; peak-normalised.
  //       - stages == 2: the bi-exponential convolved with one more RC
  //         integrator of tau = shaper_extra_stage_tau_ns (CR-RC-RC), which
  //         broadens the pulse into a semi-gaussian.  stages > 2 adds further
  //         identical RC stages.
  //   * Measured: the caller supplies (measured_impulse_t_ns,
  //     measured_impulse_amplitude), which is linearly interpolated onto the
  //     sample grid and peak-normalised.  This is the hook for a bench
  //     single-PE measurement; until that is supplied the generic response is
  //     used and the provenance stays ASSUMPTION_GENERIC_CRRC_NOT_MEASURED.
  double pulse_rise_ns = 1.0;            // bi-exp rise tau (RC integrator)
  double pulse_decay_ns = 25.0;          // bi-exp decay tau (CR differentiator)
  int shaper_integrator_stages = 1;      // 1 = CR-RC, 2 = CR-RC-RC, ...
  double shaper_extra_stage_tau_ns = 25.0;  // tau of each extra RC stage
  std::vector<double> measured_impulse_t_ns;     // empty -> generic shaper
  std::vector<double> measured_impulse_amplitude;

  double electronics_noise_sigma_pe = 0.03;
  double baseline_adc = 200.0;
  double adc_lsb_pe = 0.01;
  int adc_bits = 12;

  // Provenance carried with the config so every run can record it.
  DeviceProfileProvenance device_provenance;
  ElectronicsProvenance electronics_provenance;

  std::size_t max_candidates = 1000000;

  int number_of_cells() const { return cells_x * cells_y; }
  void validate() const;

  // Apply CCB_SIPM_* environment overrides to an already-populated config.
  // Recognised keys: CCB_SIPM_WINDOW_START_NS, CCB_SIPM_WINDOW_END_NS,
  // CCB_SIPM_SAMPLE_DT_NS, CCB_SIPM_SHAPER_STAGES,
  // CCB_SIPM_SHAPER_TAU_NS, CCB_SIPM_SHAPER_EXTRA_TAU_NS,
  // CCB_SIPM_ADC_BITS, CCB_SIPM_ADC_LSB_PE, CCB_SIPM_BASELINE_ADC,
  // CCB_SIPM_PDE_SCALE, CCB_SIPM_OVERVOLTAGE_V, CCB_SIPM_TEMPERATURE_C.
  // Returns the number of keys applied; unknown keys are ignored.
  static int ApplyEnvironmentOverrides(ModelConfig& config);

  static ModelConfig RepresentativeS13360_3050CS();
};

// Run-level metadata: the device profile + sources + electronics provenance
// that produced the SiPM response, plus the model parameters a downstream
// analyst needs to interpret the waveforms.  render_json() emits a
// dependency-free JSON block suitable for writing into a run metadata file.
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
  std::string render_json() const;
};

}  // namespace ccb::sipm
