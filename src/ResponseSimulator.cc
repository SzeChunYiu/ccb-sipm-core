#include "ccb/sipm/ResponseSimulator.hh"

#include "ccb/sipm/Seed.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <random>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ccb::sipm {

namespace {

struct Candidate {
  double time_ns = 0.0;
  AvalancheType type = AvalancheType::Photon;
  int cell_id = -1;
  std::uint64_t parent_index = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t sequence = 0;
};

struct CandidateLater {
  bool operator()(const Candidate& a, const Candidate& b) const {
    if (a.time_ns != b.time_ns) return a.time_ns > b.time_ns;
    return a.sequence > b.sequence;
  }
};

double Clamp01(double x) {
  return std::max(0.0, std::min(1.0, x));
}

}  // namespace

ResponseSimulator::ResponseSimulator(ModelConfig config)
    : config_(std::move(config)) {
  config_.validate();
}

double ResponseSimulator::photon_detection_efficiency(
    double wavelength_nm) const {
  const auto& curve = config_.pde_curve;
  if (!std::isfinite(wavelength_nm) ||
      wavelength_nm < curve.front().wavelength_nm ||
      wavelength_nm > curve.back().wavelength_nm) {
    return 0.0;
  }
  const auto upper = std::lower_bound(
      curve.begin(), curve.end(), wavelength_nm,
      [](const PdePoint& point, double value) {
        return point.wavelength_nm < value;
      });
  double base = 0.0;
  if (upper == curve.begin()) {
    base = upper->pde;
  } else if (upper == curve.end()) {
    base = curve.back().pde;
  } else if (upper->wavelength_nm == wavelength_nm) {
    base = upper->pde;
  } else {
    const auto lower = upper - 1;
    const double fraction =
        (wavelength_nm - lower->wavelength_nm) /
        (upper->wavelength_nm - lower->wavelength_nm);
    base = lower->pde + fraction * (upper->pde - lower->pde);
  }
  return Clamp01(base * config_.pde_scale *
                 config_.coupling_efficiency);
}

int ResponseSimulator::cell_from_position(double x_mm, double y_mm) const {
  const double half_x = 0.5 * config_.active_width_mm;
  const double half_y = 0.5 * config_.active_height_mm;
  if (!std::isfinite(x_mm) || !std::isfinite(y_mm) ||
      x_mm < -half_x || x_mm > half_x ||
      y_mm < -half_y || y_mm > half_y) {
    return -1;
  }

  const double ux = (x_mm + half_x) / config_.active_width_mm;
  const double uy = (y_mm + half_y) / config_.active_height_mm;
  const int ix = std::min(config_.cells_x - 1,
                          static_cast<int>(std::floor(ux * config_.cells_x)));
  const int iy = std::min(config_.cells_y - 1,
                          static_cast<int>(std::floor(uy * config_.cells_y)));
  if (ix < 0 || iy < 0) return -1;
  return iy * config_.cells_x + ix;
}

EventResult ResponseSimulator::simulate(
    const std::vector<PhotonArrival>& arrivals,
    std::uint64_t run_seed,
    std::uint64_t event_id) const {
  EventResult result;
  result.event_id = event_id;
  result.sensor_id = config_.sensor_id;
  result.n_incident_photons = arrivals.size();

  std::mt19937_64 rng(MakeEventSeed(run_seed, event_id,
                                    static_cast<std::uint64_t>(config_.sensor_id), 0));
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  std::uniform_int_distribution<int> random_cell(0, config_.number_of_cells() - 1);
  std::normal_distribution<double> sptr(0.0, config_.sptr_sigma_ns);

  std::priority_queue<Candidate, std::vector<Candidate>, CandidateLater> queue;
  std::uint64_t sequence = 0;

  auto schedule = [&](double time_ns, AvalancheType type, int cell_id,
                      std::uint64_t parent_index) {
    if (time_ns < config_.window_start_ns ||
        time_ns > config_.window_end_ns) {
      return;
    }
    queue.push(Candidate{time_ns, type, cell_id, parent_index, sequence++});
  };

  for (const auto& photon : arrivals) {
    if (photon.sensor_id != config_.sensor_id) continue;
    int cell = photon.has_local_position
                   ? cell_from_position(photon.x_mm, photon.y_mm)
                   : random_cell(rng);
    if (cell < 0) {
      ++result.n_outside_active_area;
      continue;
    }
    const double pde = photon_detection_efficiency(photon.wavelength_nm);
    if (unit(rng) < pde) {
      ++result.n_primary_candidates;
      schedule(photon.time_ns + sptr(rng), AvalancheType::Photon, cell,
               std::numeric_limits<std::uint64_t>::max());
    }
  }

  if (config_.enable_dark_counts && config_.dark_count_rate_hz > 0.0) {
    const double duration_s =
        (config_.window_end_ns - config_.window_start_ns) * 1.0e-9;
    std::poisson_distribution<std::size_t> n_dark(
        config_.dark_count_rate_hz * duration_s);
    result.n_dark_candidates = n_dark(rng);
    std::uniform_real_distribution<double> dark_time(
        config_.window_start_ns, config_.window_end_ns);
    for (std::size_t i = 0; i < result.n_dark_candidates; ++i) {
      schedule(dark_time(rng), AvalancheType::Dark, random_cell(rng),
               std::numeric_limits<std::uint64_t>::max());
    }
  }

  std::vector<double> last_fire(
      static_cast<std::size_t>(config_.number_of_cells()),
      -std::numeric_limits<double>::infinity());

  auto neighbour_cells = [&](int cell_id) {
    std::vector<int> cells;
    const int x = cell_id % config_.cells_x;
    const int y = cell_id / config_.cells_x;
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0) continue;
        if (config_.crosstalk_neighbourhood == 4 &&
            std::abs(dx) + std::abs(dy) != 1) {
          continue;
        }
        const int nx = x + dx;
        const int ny = y + dy;
        if (nx >= 0 && nx < config_.cells_x &&
            ny >= 0 && ny < config_.cells_y) {
          cells.push_back(ny * config_.cells_x + nx);
        }
      }
    }
    return cells;
  };

  while (!queue.empty()) {
    if (result.n_candidates_processed >= config_.max_candidates) {
      result.candidate_limit_reached = true;
      break;
    }
    Candidate candidate = queue.top();
    queue.pop();
    ++result.n_candidates_processed;

    if (candidate.cell_id < 0 ||
        candidate.cell_id >= config_.number_of_cells()) {
      ++result.n_rejected_dead_or_recovery;
      continue;
    }

    const double previous =
        last_fire[static_cast<std::size_t>(candidate.cell_id)];
    const bool never_fired = !std::isfinite(previous);
    const double dt = never_fired
                          ? std::numeric_limits<double>::infinity()
                          : candidate.time_ns - previous;

    if (!never_fired &&
        (dt <= config_.dead_time_ns || dt < 0.0)) {
      ++result.n_rejected_dead_or_recovery;
      continue;
    }

    const double recovery = never_fired
                                ? 1.0
                                : Clamp01(1.0 - std::exp(-dt /
                                                       config_.recovery_time_ns));
    if (unit(rng) > recovery) {
      ++result.n_rejected_dead_or_recovery;
      continue;
    }

    std::normal_distribution<double> gain(
        config_.gain_mean_pe,
        config_.gain_mean_pe * config_.gain_sigma_fraction);
    const double amplitude = std::max(0.0, gain(rng)) * recovery;

    Avalanche avalanche;
    avalanche.index = result.avalanches.size();
    avalanche.type = candidate.type;
    avalanche.parent_index = candidate.parent_index;
    avalanche.cell_id = candidate.cell_id;
    avalanche.cell_x = candidate.cell_id % config_.cells_x;
    avalanche.cell_y = candidate.cell_id / config_.cells_x;
    avalanche.time_ns = candidate.time_ns;
    avalanche.amplitude_pe = amplitude;
    avalanche.recovery_fraction = recovery;
    avalanche.delta_since_last_fire_ns = dt;
    result.avalanches.push_back(avalanche);

    last_fire[static_cast<std::size_t>(candidate.cell_id)] =
        candidate.time_ns;

    const std::uint64_t this_index = avalanche.index;

    if (config_.enable_prompt_crosstalk &&
        config_.prompt_crosstalk_probability > 0.0) {
      const double lambda =
          -std::log1p(-config_.prompt_crosstalk_probability);
      std::poisson_distribution<int> multiplicity(lambda * recovery);
      const int n = multiplicity(rng);
      const auto neighbours = neighbour_cells(candidate.cell_id);
      if (!neighbours.empty()) {
        std::uniform_int_distribution<std::size_t> choose(
            0, neighbours.size() - 1);
        std::normal_distribution<double> jitter(
            0.0, config_.prompt_crosstalk_jitter_ns);
        for (int i = 0; i < n; ++i) {
          schedule(candidate.time_ns + std::abs(jitter(rng)),
                   AvalancheType::PromptCrosstalk,
                   neighbours[choose(rng)], this_index);
        }
      }
    }

    if (config_.enable_delayed_crosstalk &&
        unit(rng) < config_.delayed_crosstalk_probability * recovery) {
      std::exponential_distribution<double> delay(
          1.0 / config_.delayed_crosstalk_tau_ns);
      const auto neighbours = neighbour_cells(candidate.cell_id);
      if (!neighbours.empty()) {
        std::uniform_int_distribution<std::size_t> choose(
            0, neighbours.size() - 1);
        schedule(candidate.time_ns + delay(rng),
                 AvalancheType::DelayedCrosstalk,
                 neighbours[choose(rng)], this_index);
      }
    }

    if (config_.enable_afterpulsing &&
        unit(rng) < config_.afterpulse_fast_probability * recovery) {
      std::exponential_distribution<double> delay(
          1.0 / config_.afterpulse_fast_tau_ns);
      schedule(candidate.time_ns + delay(rng),
               AvalancheType::AfterpulseFast,
               candidate.cell_id, this_index);
    }
    if (config_.enable_afterpulsing &&
        unit(rng) < config_.afterpulse_slow_probability * recovery) {
      std::exponential_distribution<double> delay(
          1.0 / config_.afterpulse_slow_tau_ns);
      schedule(candidate.time_ns + delay(rng),
               AvalancheType::AfterpulseSlow,
               candidate.cell_id, this_index);
    }
  }

  if (config_.generate_waveform) {
    result.waveform = make_waveform(
        result.avalanches,
        MakeEventSeed(run_seed, event_id,
                      static_cast<std::uint64_t>(config_.sensor_id), 1));
  }
  return result;
}

Waveform ResponseSimulator::make_waveform(
    const std::vector<Avalanche>& avalanches,
    std::uint64_t waveform_seed) const {
  Waveform waveform;
  const std::size_t n_samples = static_cast<std::size_t>(
      std::floor((config_.window_end_ns - config_.window_start_ns) /
                 config_.sample_dt_ns)) + 1U;
  waveform.time_ns.resize(n_samples);
  waveform.signal_pe.assign(n_samples, 0.0);
  waveform.analog_pe.assign(n_samples, 0.0);
  waveform.adc.assign(n_samples, 0);

  const double peak_time =
      std::log(config_.pulse_decay_ns / config_.pulse_rise_ns) /
      (1.0 / config_.pulse_rise_ns - 1.0 / config_.pulse_decay_ns);
  const double peak_value =
      std::exp(-peak_time / config_.pulse_decay_ns) -
      std::exp(-peak_time / config_.pulse_rise_ns);

  for (std::size_t i = 0; i < n_samples; ++i) {
    const double t = config_.window_start_ns +
                     static_cast<double>(i) * config_.sample_dt_ns;
    waveform.time_ns[i] = t;
    double signal = 0.0;
    for (const auto& avalanche : avalanches) {
      const double dt = t - avalanche.time_ns;
      if (dt < 0.0) continue;
      const double raw = std::exp(-dt / config_.pulse_decay_ns) -
                         std::exp(-dt / config_.pulse_rise_ns);
      signal += avalanche.amplitude_pe * raw / peak_value;
    }
    waveform.signal_pe[i] = signal;
  }

  std::mt19937_64 rng(waveform_seed);
  std::normal_distribution<double> noise(
      0.0, config_.electronics_noise_sigma_pe);
  const int max_adc = (1 << config_.adc_bits) - 1;
  for (std::size_t i = 0; i < n_samples; ++i) {
    waveform.analog_pe[i] = waveform.signal_pe[i] + noise(rng);
    const long code = std::lround(
        config_.baseline_adc + waveform.analog_pe[i] / config_.adc_lsb_pe);
    waveform.adc[i] =
        static_cast<int>(std::max<long>(0, std::min<long>(max_adc, code)));
  }
  return waveform;
}

}  // namespace ccb::sipm
