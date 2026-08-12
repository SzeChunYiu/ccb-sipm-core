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
  std::string impulse_response_status;  // "ASSUMPTION_GENERIC_CRRC_NOT_MEASURED"
  std::string shaper_model;             // "CR-RC(-RC) semi-gaussian, configurable stages"
  int integrator_stages = 1;            // 1 = CR-RC bi-exponential, 2 = CR-RC-RC
  std::string measured_impulse_source_id;        // "" when generic
  std::string measured_impulse_source_url;       // "" when generic
  std::string measured_impulse_retrieved_date;   // "" when generic
  // SHA-256 hex digest of the source bytes (measured_impulse_t_ns + amplitude
  // vectors serialised as JSON).  Empty when generic.  This lets downstream
  // consumers verify that the same source impulse was used across runs.
  std::string measured_impulse_source_hash = "";
  // SHA-256 hex digest of the resampled-and-peak-normalised kernel on the
  // runtime grid.  Empty when generic.  Two runs with the same effective_kernel
  // hash produce identical analog waveforms (same noise seed aside).
  std::string effective_kernel_hash = "";
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

  // Recovery model-form selection (issue #1066, ARU-SIPM-RECOVERY-LAW-001).
  // The microcell's single charge-recovery constant tau is used in TWO distinct
  // places that the original code conflated into one shared factor:
  //   1. trigger acceptance:  P(cell fires | it fired dt ago) = r(dt);
  //   2. gain of that fire:   amplitude scales with recovered charge = g(dt).
  // The legacy/implicit model set both equal to r(dt) = 1 - exp(-dt/tau), which
  // makes the unconditional second-fire charge E[Q2] = r(dt)^2 (the H1 law).
  // That is a modelling *assumption*, not a measured truth, so each site has an
  // explicit named model and H1 is preserved as the default reduced model.
  //
  // trigger_recovery_model: which function gates whether a candidate fire is
  //   admitted.  "EXPONENTIAL" -> r(dt) = 1 - exp(-dt/tau).
  // gain_recovery_model: which function scales the fired avalanche's amplitude.
  //   "EXPONENTIAL_H1_SHARED" -> g(dt) = r(dt)  (reproduces the legacy H1 r^2 law).
  //   "FULL_RECOVERY"         -> g(dt) = 1.0    (charge independent of dt; the
  //                              alternative hypothesis, giving E[Q2] = r(dt)).
  std::string trigger_recovery_model = "EXPONENTIAL";
  std::string gain_recovery_model = "EXPONENTIAL_H1_SHARED";
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

  // Parent-avalanche recovery law used only when generating each correlated-
  // noise family.  Child candidates subsequently pass their own target-cell
  // trigger-recovery gate, so parent generation and child triggering remain
  // distinct stages.  Defaults preserve the historical raw-r coupling exactly.
  // Admitted model identifiers are defined in CorrelatedNoiseRecovery.hh:
  //   RAW_RECHARGE_LEGACY, GAIN_COUPLED_HYPOTHESIS, UNSUPPRESSED_CONTROL.
  // The latter two are explicit model-form alternatives/controls, not claims
  // about the CCB detector truth. Fast and slow afterpulse components are kept
  // separate so their recovery coupling is not silently tied together.
  std::string prompt_crosstalk_parent_recovery_model =
      "RAW_RECHARGE_LEGACY";
  std::string delayed_crosstalk_parent_recovery_model =
      "RAW_RECHARGE_LEGACY";
  std::string afterpulse_fast_parent_recovery_model =
      "RAW_RECHARGE_LEGACY";
  std::string afterpulse_slow_parent_recovery_model =
      "RAW_RECHARGE_LEGACY";

  double window_start_ns = -20.0;
  double window_end_ns = 250.0;
  double history_start_ns = -200.0;

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

  // Declared impulse model.  "GENERIC_CRRC" (default; analytical shaper),
  // "MEASURED" (a non-degenerate measured_impulse_* is the kernel), or
  // "IDEAL_DELTA_TEST_ONLY" (unit delta at t=0; ONLY reachable when
  // authorising == true and only for tests/development, never a valid
  // physical response).  The model is validated against the measured_impulse_*
  // vectors by validate(); a degenerate measured impulse is a hard error, not
  // a silent fallback to the delta.
  // Mutable so validate() (a const method that reconciles the model from the
  // measured-impulse vectors) can set it.
  mutable std::string impulse_model = "GENERIC_CRRC";

  // Authorisation gate for the test-only IDEAL_DELTA_TEST_ONLY model.  Defaults
  // false so a production run can never claim an ideal-delta response.  Only an
  // explicit test/development harness sets it true.
  bool authorising = false;

  std::vector<double> measured_impulse_t_ns;     // empty -> generic shaper
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
// Reconciled impulse model for this run: "GENERIC_CRRC", "MEASURED" or
  // "IDEAL_DELTA_TEST_ONLY".  Mirrors ModelConfig::impulse_model after
  // validate().
  std::string impulse_model;
  std::string trigger_recovery_model;
  std::string gain_recovery_model;
  std::string prompt_crosstalk_parent_recovery_model;
  std::string delayed_crosstalk_parent_recovery_model;
  std::string afterpulse_fast_parent_recovery_model;
  std::string afterpulse_slow_parent_recovery_model;
  std::string render_json() const;
};

}  // namespace ccb::sipm
