#include "ccb/sipm/Config.hh"
#include "ccb/sipm/ResponseSimulator.hh"

#include <cmath>
#include <cstdint>
#include <iostream>

using ccb::sipm::AvalancheType;
using ccb::sipm::ModelConfig;
using ccb::sipm::ResponseSimulator;

namespace {

int g_failures = 0;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
  }
}

ModelConfig DarkHistoryConfig() {
  auto c = ModelConfig::RepresentativeS13360_3050CS();
  c.cells_x = 64;
  c.cells_y = 64;
  c.active_width_mm = 4.0;
  c.active_height_mm = 4.0;
  c.enable_dark_counts = true;
  c.enable_prompt_crosstalk = false;
  c.enable_delayed_crosstalk = false;
  c.enable_afterpulsing = false;
  c.gain_sigma_fraction = 0.0;
  c.sptr_sigma_ns = 0.0;
  c.electronics_noise_sigma_pe = 0.0;
  c.generate_waveform = false;
  c.history_start_ns = -200.0;
  c.window_start_ns = -20.0;
  c.window_end_ns = 250.0;
  c.validate();
  return c;
}

double MeanDarkCandidates(const ModelConfig& c,
                          std::uint64_t run_seed,
                          std::uint64_t event_count) {
  const ResponseSimulator sim(c);
  std::uint64_t total = 0;
  for (std::uint64_t event_id = 0; event_id < event_count; ++event_id) {
    total += sim.simulate({}, run_seed, event_id).n_dark_candidates;
  }
  return static_cast<double>(total) / static_cast<double>(event_count);
}

}  // namespace

int main() {
  // High-rate support discriminator: under the explicit-history contract,
  // dark primaries must be able to occur before the recorded sample window.
  {
    auto c = DarkHistoryConfig();
    c.dark_count_rate_hz = 1.0e9;
    c.validate();
    const ResponseSimulator sim(c);
    const auto r = sim.simulate({}, 0xD4A6C0DEULL, 1);

    Require(r.n_dark_candidates > 0, "dark-history fixture generated candidates");
    Require(r.n_candidates_processed == r.n_dark_candidates,
            "all generated dark candidates entered the scheduler");

    bool saw_pre_window = false;
    for (const auto& avalanche : r.avalanches) {
      Require(avalanche.type == AvalancheType::Dark,
              "dark-only fixture produced only dark avalanches");
      Require(avalanche.time_ns >= c.history_start_ns,
              "no accepted dark avalanche before history_start");
      Require(avalanche.time_ns <= c.window_end_ns,
              "no accepted dark avalanche after window_end");
      if (avalanche.time_ns < c.window_start_ns) {
        saw_pre_window = true;
      }
    }
    Require(saw_pre_window,
            "dark primaries have support in [history_start, window_start)");
  }

  // Measure-level discriminator at the representative 500 kHz model rate.
  // The declared interval is 450 ns, so E[N_dark/event] = 0.225.  The old
  // sample-window-only implementation used 270 ns and therefore 0.135.
  // Fixed event seeds make this regression reproducible; the tolerance is
  // intentionally much wider than the Monte Carlo standard error while still
  // separating the competing interval contracts.
  {
    auto c = DarkHistoryConfig();
    c.dark_count_rate_hz = 5.0e5;
    c.validate();
    constexpr std::uint64_t kEvents = 20000;
    const double mean = MeanDarkCandidates(c, 0x1096D4A6ULL, kEvents);
    Require(std::abs(mean - 0.225) < 0.02,
            "dark-candidate mean matches full history interval");
  }

  // Negative control: if history_start is deliberately collapsed onto the
  // sample boundary, the same model must reduce to the 270 ns observation
  // interval with E[N_dark/event] = 0.135.
  {
    auto c = DarkHistoryConfig();
    c.history_start_ns = c.window_start_ns;
    c.dark_count_rate_hz = 5.0e5;
    c.validate();
    constexpr std::uint64_t kEvents = 20000;
    const double mean = MeanDarkCandidates(c, 0x1096D4A6ULL, kEvents);
    Require(std::abs(mean - 0.135) < 0.02,
            "collapsed history reproduces sample-window-only dark measure");
  }

  if (g_failures == 0) {
    std::cout << "All dark-history tests passed\n";
    return 0;
  }
  std::cerr << g_failures << " dark-history test failure(s)\n";
  return 1;
}
