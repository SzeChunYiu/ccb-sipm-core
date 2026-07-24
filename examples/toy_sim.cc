#include "ccb/sipm/Config.hh"
#include "ccb/sipm/ResponseSimulator.hh"
#include "ccb/sipm/Seed.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <string>
#include <vector>

using ccb::sipm::AvalancheType;
using ccb::sipm::MakeEventSeed;
using ccb::sipm::ModelConfig;
using ccb::sipm::PhotonArrival;
using ccb::sipm::ResponseSimulator;
using ccb::sipm::ToString;

namespace {

bool Match(const char* arg, const char* key) {
  return std::strcmp(arg, key) == 0;
}

void PrintUsage() {
  std::cerr <<
      "usage: ccb_sipm_toy [output_dir] [options]\n"
      "options (env-equivalents in parentheses):\n"
      "  --window-start NS   readout window start (" << "CCB_SIPM_WINDOW_START_NS)\n"
      "  --window-end NS     readout window end   (" << "CCB_SIPM_WINDOW_END_NS)\n"
      "  --shaper-tau NS     CR-RC dominant decay (" << "CCB_SIPM_SHAPER_TAU_NS)\n"
      "  --shaper-stages N   1=CR-RC, 2=CR-RC-RC  (" << "CCB_SIPM_SHAPER_STAGES)\n"
      "  --adc-bits N        ADC resolution       (" << "CCB_SIPM_ADC_BITS)\n";
}

// Apply CLI overrides. Recognised flags mirror ApplyEnvironmentOverrides so
// the operator can drive the same knobs from either the command line or env.
int ApplyCliOverrides(int argc, char** argv, ModelConfig& c) {
  int applied = 0;
  for (int i = 2; i < argc; ++i) {
    const char* a = argv[i];
    const char* val = (i + 1 < argc) ? argv[++i] : nullptr;
    if (!val) { std::cerr << "missing value for " << a << "\n"; std::exit(2); }
    try {
      if (Match(a, "--window-start")) {
        c.window_start_ns = std::stod(val); ++applied;
      } else if (Match(a, "--window-end")) {
        c.window_end_ns = std::stod(val); ++applied;
      } else if (Match(a, "--shaper-tau")) {
        c.pulse_decay_ns = std::stod(val); ++applied;
      } else if (Match(a, "--shaper-stages")) {
        c.shaper_integrator_stages = std::stoi(val); ++applied;
      } else if (Match(a, "--adc-bits")) {
        c.adc_bits = std::stoi(val); ++applied;
      } else {
        std::cerr << "unknown option " << a << "\n"; std::exit(2);
      }
    } catch (const std::exception& e) {
      std::cerr << "bad value for " << a << ": " << e.what() << "\n";
      std::exit(2);
    }
  }
  return applied;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 &&
      (Match(argv[1], "-h") || Match(argv[1], "--help"))) {
    PrintUsage();
    return 0;
  }
  const std::filesystem::path out =
      (argc > 1 && argv[1][0] != '-')
          ? std::filesystem::path(argv[1])
          : std::filesystem::path("toy_output");
  std::filesystem::create_directories(out);

  auto config = ModelConfig::RepresentativeS13360_3050CS();
  config.coupling_efficiency = 0.85;
  config.enable_dark_counts = true;
  config.dark_count_rate_hz = 500000.0;
  config.prompt_crosstalk_probability = 0.03;
  config.afterpulse_fast_probability = 0.01;
  config.afterpulse_slow_probability = 0.005;
  config.recovery_time_ns = 30.0;
  config.sptr_sigma_ns = 0.12;
  config.window_start_ns = -20.0;
  config.window_end_ns = 250.0;
  config.sample_dt_ns = 0.5;
  config.generate_waveform = true;

  // Front-end / electronics knobs.  Order: representative defaults ->
  // CLI overrides -> env overrides (env wins so a Slurm batch can pin a
  // canonical configuration regardless of the demo's argv).
  (void)ApplyCliOverrides(argc, argv, config);
  (void)ModelConfig::ApplyEnvironmentOverrides(config);
  config.validate();

  ResponseSimulator simulator(config);
  constexpr std::uint64_t run_seed = 20260723;
  constexpr int n_events = 500;

  std::ofstream events(out / "events.csv");
  std::ofstream avalanches(out / "avalanches.csv");
  std::ofstream waveforms(out / "waveforms.csv");
  events << "event,sensor,n_incident,n_primary_candidates,n_dark_candidates,"
            "n_avalanches,n_photon,n_dark,n_prompt_xt,n_delayed_xt,"
            "n_after_fast,n_after_slow,n_unique_cells,charge_pe,peak_pe,"
            "first_time_ns,n_rejected,candidate_limit\n";
  avalanches << "event,sensor,index,parent_index,type,time_ns,cell_id,cell_x,"
                "cell_y,amplitude_pe,recovery_fraction,delta_since_last_fire_ns\n";
  waveforms << "event,sensor,sample,time_ns,signal_pe,analog_pe,adc\n";

  std::poisson_distribution<int> photon_count(90.0);
  std::normal_distribution<double> wavelength(476.0, 18.0);
  std::exponential_distribution<double> scint_delay(1.0 / 2.4);
  std::exponential_distribution<double> wls_delay(1.0 / 8.5);
  std::normal_distribution<double> transport_jitter(1.4, 0.35);
  std::uniform_real_distribution<double> xy(-0.9, 0.9);

  for (int event = 0; event < n_events; ++event) {
    std::mt19937_64 source_rng(
        MakeEventSeed(run_seed, static_cast<std::uint64_t>(event), 0, 99));
    std::vector<PhotonArrival> arrivals;
    const int n = photon_count(source_rng);
    arrivals.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
      double x = 0.0;
      double y = 0.0;
      do {
        x = xy(source_rng);
        y = xy(source_rng);
      } while (x * x + y * y > 0.9 * 0.9);
      PhotonArrival p;
      p.photon_id = static_cast<std::uint64_t>(i);
      p.sensor_id = 0;
      p.time_ns = scint_delay(source_rng) + wls_delay(source_rng) +
                  std::max(0.0, transport_jitter(source_rng));
      p.wavelength_nm = std::clamp(wavelength(source_rng), 300.0, 700.0);
      p.x_mm = x;
      p.y_mm = y;
      arrivals.push_back(p);
    }

    const auto result =
        simulator.simulate(arrivals, run_seed,
                           static_cast<std::uint64_t>(event));
    std::set<int> cells;
    double charge = 0.0;
    double first_time = std::numeric_limits<double>::quiet_NaN();
    for (const auto& a : result.avalanches) {
      cells.insert(a.cell_id);
      charge += a.amplitude_pe;
      if (!std::isfinite(first_time) || a.time_ns < first_time) {
        first_time = a.time_ns;
      }
      avalanches << event << "," << result.sensor_id << "," << a.index << ","
                 << a.parent_index << "," << ToString(a.type) << ","
                 << std::setprecision(12) << a.time_ns << "," << a.cell_id
                 << "," << a.cell_x << "," << a.cell_y << ","
                 << a.amplitude_pe << "," << a.recovery_fraction << ","
                 << a.delta_since_last_fire_ns << "\n";
    }
    const double peak =
        result.waveform.signal_pe.empty()
            ? 0.0
            : *std::max_element(result.waveform.signal_pe.begin(),
                                result.waveform.signal_pe.end());

    events << event << "," << result.sensor_id << ","
           << result.n_incident_photons << ","
           << result.n_primary_candidates << ","
           << result.n_dark_candidates << ","
           << result.avalanches.size() << ","
           << result.count(AvalancheType::Photon) << ","
           << result.count(AvalancheType::Dark) << ","
           << result.count(AvalancheType::PromptCrosstalk) << ","
           << result.count(AvalancheType::DelayedCrosstalk) << ","
           << result.count(AvalancheType::AfterpulseFast) << ","
           << result.count(AvalancheType::AfterpulseSlow) << ","
           << cells.size() << "," << charge << "," << peak << ","
           << first_time << ","
           << result.n_rejected_dead_or_recovery << ","
           << (result.candidate_limit_reached ? 1 : 0) << "\n";

    if (event < 12) {
      for (std::size_t i = 0; i < result.waveform.time_ns.size(); ++i) {
        waveforms << event << "," << result.sensor_id << "," << i << ","
                  << result.waveform.time_ns[i] << ","
                  << result.waveform.signal_pe[i] << ","
                  << result.waveform.analog_pe[i] << ","
                  << result.waveform.adc[i] << "\n";
      }
    }
  }

  // Run metadata: device profile + sources + generic-electronics provenance
  // from the library, augmented with the demo-specific bookkeeping.  This is
  // the hook a downstream Geant4 integration would use to record which device
  // profile + sources produced each SiPM campaign.
  std::string lib_meta = simulator.run_metadata().render_json();
  if (!lib_meta.empty() && lib_meta.back() == '\n') lib_meta.pop_back();
  if (!lib_meta.empty() && lib_meta.back() == '}') lib_meta.pop_back();
  std::ofstream meta(out / "metadata.json");
  meta << lib_meta << ",\n"
       << "  \"run\": {\n"
       << "    \"status\": \"SYNTHETIC_DEMO_ONLY\",\n"
       << "    \"run_seed\": " << run_seed << ",\n"
       << "    \"n_events\": " << n_events << ",\n"
       << "    \"sensor_model\": \"representative_not_calibrated\",\n"
       << "    \"wls_generation\": \"toy_exponential_8.5_ns\",\n"
       << "    \"note\": \"No Geant4 transport or detector data is used. "
          "Device and electronics blocks are the ccb_sipm_core library run "
          "metadata.\"\n"
       << "  }\n}\n";

  std::cout << "Wrote synthetic toy campaign to " << out << "\n";
  return 0;
}
