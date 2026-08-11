#include "ccb/sipm/Config.hh"
#include "ccb/sipm/ResponseSimulator.hh"
#include "ccb/sipm/Types.hh"

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

ccb::sipm::ModelConfig DarkHistoryConfig() {
  auto c = ccb::sipm::ModelConfig::RepresentativeS13360_3050CS();
  c.cells_x = 100;
  c.cells_y = 100;
  c.active_width_mm = 100.0;
  c.active_height_mm = 100.0;
  c.enable_dark_counts = true;
  c.dark_count_rate_hz = 5.0e6;
  c.enable_prompt_crosstalk = false;
  c.enable_delayed_crosstalk = false;
  c.enable_afterpulsing = false;
  c.gain_sigma_fraction = 0.0;
  c.sptr_sigma_ns = 0.0;
  c.dead_time_ns = 0.0;
  c.recovery_time_ns = 1.0e-6;
  c.generate_waveform = false;
  c.history_start_ns = -200.0;
  c.window_start_ns = 0.0;
  c.window_end_ns = 100.0;
  c.validate();
  return c;
}

}  // namespace

int main() {
  constexpr std::uint64_t run_seed = 0x5a17d4c3ULL;
  constexpr std::size_t n_events = 4000;

  const auto history_config = DarkHistoryConfig();
  auto window_only_config = history_config;
  window_only_config.history_start_ns = window_only_config.window_start_ns;
  window_only_config.validate();

  const ccb::sipm::ResponseSimulator history_sim(history_config);
  const ccb::sipm::ResponseSimulator window_only_sim(window_only_config);

  std::size_t history_candidates = 0;
  std::size_t window_only_candidates = 0;
  std::size_t prewindow_dark_avalanches = 0;
  bool support_violation = false;

  for (std::size_t event = 0; event < n_events; ++event) {
    const auto history = history_sim.simulate({}, run_seed, event);
    const auto window_only = window_only_sim.simulate({}, run_seed, event);
    history_candidates += history.n_dark_candidates;
    window_only_candidates += window_only.n_dark_candidates;

    for (const auto& avalanche : history.avalanches) {
      if (avalanche.type != ccb::sipm::AvalancheType::Dark) continue;
      if (avalanche.time_ns < history_config.history_start_ns ||
          avalanche.time_ns > history_config.window_end_ns) {
        support_violation = true;
      }
      if (avalanche.time_ns < history_config.window_start_ns) {
        ++prewindow_dark_avalanches;
      }
    }
  }

  // Homogeneous Poisson law: mu = R * Delta t.  The history-inclusive
  // interval is 300 ns while the sample-only interval is 100 ns, so the
  // expected candidate-count ratio is exactly 3.  A wide deterministic
  // acceptance band avoids coupling this regression to one libstdc++ draw
  // sequence while decisively rejecting the old window-only implementation.
  Require(window_only_candidates > 0,
          "window-only DCR control must generate candidates");
  Require(history_candidates > 2 * window_only_candidates,
          "history-inclusive DCR candidate measure must exceed 2x control");
  Require(history_candidates < 4 * window_only_candidates,
          "history-inclusive DCR candidate measure must remain near 3x control");

  Require(!support_violation,
          "dark avalanches must stay inside declared history support");
  Require(prewindow_dark_avalanches > 0,
          "history-inclusive DCR must produce pre-window dark avalanches");

  if (failures != 0) {
    std::cerr << failures << " dark-history test(s) failed\n";
    return 1;
  }

  std::cout << "dark-history support OK: history_candidates="
            << history_candidates
            << " window_only_candidates=" << window_only_candidates
            << " prewindow_dark_avalanches=" << prewindow_dark_avalanches
            << '\n';
  return 0;
}
