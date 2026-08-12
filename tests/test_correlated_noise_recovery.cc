#include "ccb/sipm/Config.hh"
#include "ccb/sipm/CorrelatedNoiseRecovery.hh"
#include "ccb/sipm/ResponseSimulator.hh"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using ccb::sipm::Avalanche;
using ccb::sipm::EventResult;
using ccb::sipm::ModelConfig;
using ccb::sipm::PhotonArrival;
using ccb::sipm::ResponseSimulator;
using ccb::sipm::EvaluateCorrelatedNoiseParentRecovery;
using ccb::sipm::kParentRecoveryGainCoupledHypothesis;
using ccb::sipm::kParentRecoveryRawRechargeLegacy;
using ccb::sipm::kParentRecoveryUnsuppressedControl;

ModelConfig BaseConfig() {
  ModelConfig c = ModelConfig::RepresentativeS13360_3050CS();
  c.cells_x = 3;
  c.cells_y = 3;
  c.active_width_mm = 3.0;
  c.active_height_mm = 3.0;
  c.pde_curve = {{400.0, 1.0}, {500.0, 1.0}};
  c.pde_scale = 1.0;
  c.coupling_efficiency = 1.0;
  c.recovery_time_ns = 30.0;
  c.dead_time_ns = 0.0;
  c.gain_mean_pe = 1.0;
  c.gain_sigma_fraction = 0.0;
  c.sptr_sigma_ns = 0.0;
  c.enable_dark_counts = false;
  c.enable_prompt_crosstalk = true;
  c.prompt_crosstalk_probability = 0.20;
  c.prompt_crosstalk_jitter_ns = 0.0;
  c.crosstalk_neighbourhood = 4;
  c.enable_delayed_crosstalk = false;
  c.delayed_crosstalk_probability = 0.0;
  c.enable_afterpulsing = false;
  c.afterpulse_fast_probability = 0.0;
  c.afterpulse_slow_probability = 0.0;
  c.history_start_ns = -100.0;
  c.window_start_ns = -100.0;
  c.window_end_ns = 300.0;
  c.generate_waveform = false;
  return c;
}

std::vector<PhotonArrival> TwoPulseFixture() {
  return {
      PhotonArrival{0, 0, 0.0, 450.0, 0.0, 0.0, true},
      PhotonArrival{1, 0, 30.0, 450.0, 0.0, 0.0, true},
  };
}

bool SameAvalanche(const Avalanche& a, const Avalanche& b) {
  return a.index == b.index && a.type == b.type &&
         a.parent_index == b.parent_index && a.cell_id == b.cell_id &&
         a.cell_x == b.cell_x && a.cell_y == b.cell_y &&
         a.time_ns == b.time_ns && a.amplitude_pe == b.amplitude_pe &&
         a.recovery_fraction == b.recovery_fraction &&
         a.delta_since_last_fire_ns == b.delta_since_last_fire_ns;
}

bool SameEvent(const EventResult& a, const EventResult& b) {
  if (a.event_id != b.event_id || a.sensor_id != b.sensor_id ||
      a.n_incident_photons != b.n_incident_photons ||
      a.n_outside_active_area != b.n_outside_active_area ||
      a.n_primary_candidates != b.n_primary_candidates ||
      a.n_dark_candidates != b.n_dark_candidates ||
      a.n_rejected_dead_or_recovery != b.n_rejected_dead_or_recovery ||
      a.n_candidates_processed != b.n_candidates_processed ||
      a.candidate_limit_reached != b.candidate_limit_reached ||
      a.avalanches.size() != b.avalanches.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.avalanches.size(); ++i) {
    if (!SameAvalanche(a.avalanches[i], b.avalanches[i])) return false;
  }
  return true;
}

std::size_t PromptCountAcrossEvents(const ModelConfig& config,
                                    std::uint64_t run_seed,
                                    std::size_t n_events) {
  const ResponseSimulator sim(config);
  const auto arrivals = TwoPulseFixture();
  std::size_t total = 0;
  for (std::size_t i = 0; i < n_events; ++i) {
    const auto event = sim.simulate(arrivals, run_seed, i);
    assert(!event.candidate_limit_reached);
    total += event.count(ccb::sipm::AvalancheType::PromptCrosstalk);
  }
  return total;
}

void ExpectInvalidModel(const char* which) {
  ModelConfig c = BaseConfig();
  if (std::string(which) == "prompt") {
    c.prompt_crosstalk_parent_recovery_model = "NOT_A_MODEL";
  } else if (std::string(which) == "delayed") {
    c.delayed_crosstalk_parent_recovery_model = "NOT_A_MODEL";
  } else {
    c.afterpulse_parent_recovery_model = "NOT_A_MODEL";
  }
  bool threw = false;
  try {
    ResponseSimulator ignored(c);
    (void)ignored;
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert(threw);
}

}  // namespace

int main() {
  const double r_tau = 1.0 - std::exp(-1.0);
  assert(std::abs(r_tau - 0.6321205588285577) < 1.0e-15);

  // Exact mathematical contract for the three explicitly named hypotheses.
  assert(EvaluateCorrelatedNoiseParentRecovery(
             kParentRecoveryRawRechargeLegacy, r_tau, 1.0) == r_tau);
  assert(EvaluateCorrelatedNoiseParentRecovery(
             kParentRecoveryGainCoupledHypothesis, r_tau, 1.0) == 1.0);
  assert(EvaluateCorrelatedNoiseParentRecovery(
             kParentRecoveryUnsuppressedControl, r_tau, 0.25) == 1.0);
  assert(EvaluateCorrelatedNoiseParentRecovery(
             kParentRecoveryRawRechargeLegacy, 1.0, 1.0) == 1.0);
  assert(EvaluateCorrelatedNoiseParentRecovery(
             kParentRecoveryGainCoupledHypothesis, 1.0, 1.0) == 1.0);

  bool bad_factor_threw = false;
  try {
    (void)EvaluateCorrelatedNoiseParentRecovery(
        kParentRecoveryRawRechargeLegacy, -0.1, 1.0);
  } catch (const std::invalid_argument&) {
    bad_factor_threw = true;
  }
  assert(bad_factor_threw);

  ExpectInvalidModel("prompt");
  ExpectInvalidModel("delayed");
  ExpectInvalidModel("afterpulse");

  // Metadata must expose all parent-generation model choices exactly.
  ModelConfig metadata_config = BaseConfig();
  metadata_config.prompt_crosstalk_parent_recovery_model =
      kParentRecoveryGainCoupledHypothesis;
  metadata_config.delayed_crosstalk_parent_recovery_model =
      kParentRecoveryUnsuppressedControl;
  metadata_config.afterpulse_parent_recovery_model =
      kParentRecoveryRawRechargeLegacy;
  const ResponseSimulator metadata_sim(metadata_config);
  const auto metadata = metadata_sim.run_metadata();
  assert(metadata.prompt_crosstalk_parent_recovery_model ==
         kParentRecoveryGainCoupledHypothesis);
  assert(metadata.delayed_crosstalk_parent_recovery_model ==
         kParentRecoveryUnsuppressedControl);
  assert(metadata.afterpulse_parent_recovery_model ==
         kParentRecoveryRawRechargeLegacy);
  const std::string json = metadata.render_json();
  assert(json.find("\"prompt_crosstalk_parent_recovery_model\": "
                   "\"GAIN_COUPLED_HYPOTHESIS\"") != std::string::npos);
  assert(json.find("\"delayed_crosstalk_parent_recovery_model\": "
                   "\"UNSUPPRESSED_CONTROL\"") != std::string::npos);
  assert(json.find("\"afterpulse_parent_recovery_model\": "
                   "\"RAW_RECHARGE_LEGACY\"") != std::string::npos);

  const auto arrivals = TwoPulseFixture();
  constexpr std::uint64_t kSeed = 0x4f2a17c3ULL;

  // Identifiability collapse: under legacy H1 gain recovery, g=r, so the raw-r
  // and gain-coupled parent-generation models must be exactly observationally
  // identical for the same event seed.
  ModelConfig h1_raw = BaseConfig();
  h1_raw.gain_recovery_model = "EXPONENTIAL_H1_SHARED";
  h1_raw.prompt_crosstalk_parent_recovery_model =
      kParentRecoveryRawRechargeLegacy;
  ModelConfig h1_gain = h1_raw;
  h1_gain.prompt_crosstalk_parent_recovery_model =
      kParentRecoveryGainCoupledHypothesis;
  const ResponseSimulator h1_raw_sim(h1_raw);
  const ResponseSimulator h1_gain_sim(h1_gain);
  for (std::uint64_t event_id = 0; event_id < 256; ++event_id) {
    assert(SameEvent(h1_raw_sim.simulate(arrivals, kSeed, event_id),
                     h1_gain_sim.simulate(arrivals, kSeed, event_id)));
  }

  // Under FULL_RECOVERY gain, gain-coupled and unsuppressed are both exactly 1
  // and therefore collapse to the same simulator output.  This is a deliberate
  // control, not a physical selection.
  ModelConfig full_gain = BaseConfig();
  full_gain.gain_recovery_model = "FULL_RECOVERY";
  full_gain.prompt_crosstalk_parent_recovery_model =
      kParentRecoveryGainCoupledHypothesis;
  ModelConfig full_unsuppressed = full_gain;
  full_unsuppressed.prompt_crosstalk_parent_recovery_model =
      kParentRecoveryUnsuppressedControl;
  const ResponseSimulator full_gain_sim(full_gain);
  const ResponseSimulator full_unsuppressed_sim(full_unsuppressed);
  for (std::uint64_t event_id = 0; event_id < 256; ++event_id) {
    assert(SameEvent(full_gain_sim.simulate(arrivals, kSeed, event_id),
                     full_unsuppressed_sim.simulate(arrivals, kSeed, event_id)));
  }

  // Fixed-seed integration discriminator.  At the second pulse dt=tau, raw-r
  // uses 0.6321 while gain-coupled under FULL_RECOVERY uses 1.  The first pulse
  // and its descendants have r=g=1, so both streams are identical until a
  // partially recovered accepted parent is reached.  Aggregate only as a
  // software-law falsifier; this is not detector validation.
  ModelConfig full_raw = BaseConfig();
  full_raw.gain_recovery_model = "FULL_RECOVERY";
  full_raw.prompt_crosstalk_parent_recovery_model =
      kParentRecoveryRawRechargeLegacy;
  ModelConfig full_gain_hypothesis = full_raw;
  full_gain_hypothesis.prompt_crosstalk_parent_recovery_model =
      kParentRecoveryGainCoupledHypothesis;
  constexpr std::size_t kEvents = 20000;
  const std::size_t raw_prompt =
      PromptCountAcrossEvents(full_raw, kSeed, kEvents);
  const std::size_t gain_prompt =
      PromptCountAcrossEvents(full_gain_hypothesis, kSeed, kEvents);
  assert(gain_prompt > raw_prompt + 300U);

  std::cout << "correlated-noise recovery model tests: PASS\n"
            << "events_per_discriminator=" << kEvents << "\n"
            << "raw_prompt=" << raw_prompt << "\n"
            << "gain_coupled_prompt=" << gain_prompt << "\n";
  return 0;
}
