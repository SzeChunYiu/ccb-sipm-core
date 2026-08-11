#include "ccb/sipm/ResponseSimulator.hh"

#include "ccb/sipm/Digest.hh"
#include "ccb/sipm/Seed.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <random>
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

double InterpLinear(const std::vector<double>& xs,
                    const std::vector<double>& ys,
                    double x) {
  if (xs.empty()) return 0.0;
  if (x < xs.front() || x > xs.back()) return 0.0;
  const auto upper = std::lower_bound(xs.begin(), xs.end(), x);
  if (upper == xs.begin()) return ys.front();
  if (upper == xs.end()) return ys.back();
  const std::size_t hi = static_cast<std::size_t>(upper - xs.begin());
  const std::size_t lo = hi - 1;
  const double f = (x - xs[lo]) / (xs[hi] - xs[lo]);
  return ys[lo] + f * (ys[hi] - ys[lo]);
}

}  // namespace

ResponseSimulator::ResponseSimulator(ModelConfig config)
    : config_(std::move(config)) {
  config_.validate();
  if (config_.generate_waveform) {
    waveform_kernel_ = make_impulse_kernel(
        impulse_kernel_sample_count(), config_.sample_dt_ns);
  }
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
  return Clamp01(base * config_.pde_scale * config_.coupling_efficiency);
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

double ResponseSimulator::EvaluateTriggerRecovery(double r_dt) const {
  if (config_.trigger_recovery_model == "EXPONENTIAL") {
    return r_dt;
  }
  throw std::invalid_argument(
      "unknown trigger_recovery_model '" +
      config_.trigger_recovery_model + "'");
}

double ResponseSimulator::EvaluateGainRecovery(double r_dt) const {
  if (config_.gain_recovery_model == "EXPONENTIAL_H1_SHARED") {
    return r_dt;
  }
  if (config_.gain_recovery_model == "FULL_RECOVERY") {
    return 1.0;
  }
  throw std::invalid_argument(
      "unknown gain_recovery_model '" +
      config_.gain_recovery_model + "'");
}

std::size_t ResponseSimulator::waveform_sample_count() const {
  return static_cast<std::size_t>(
      std::floor((config_.window_end_ns - config_.window_start_ns) /
                 config_.sample_dt_ns)) + 1U;
}

std::size_t ResponseSimulator::impulse_kernel_sample_count() const {
  const double prehistory_span_ns =
      std::max(0.0, config_.window_start_ns - config_.history_start_ns);
  const std::size_t prehistory_samples = static_cast<std::size_t>(
      std::ceil(prehistory_span_ns / config_.sample_dt_ns));
  return waveform_sample_count() + prehistory_samples;
}

EventResult ResponseSimulator::simulate(
    const std::vector<PhotonArrival>& arrivals,
    std::uint64_t run_seed,
    std::uint64_t event_id) const {
  EventResult result;
  result.event_id = event_id;
  result.sensor_id = config_.sensor_id;
  result.n_incident_photons = arrivals.size();

  std::mt19937_64 rng(MakeEventSeed(
      run_seed, event_id, static_cast<std::uint64_t>(config_.sensor_id), 0));
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  std::uniform_int_distribution<int> random_cell(
      0, config_.number_of_cells() - 1);
  std::normal_distribution<double> sptr(0.0, config_.sptr_sigma_ns);

  std::priority_queue<Candidate, std::vector<Candidate>, CandidateLater> queue;
  std::uint64_t sequence = 0;

  auto schedule = [&](double time_ns, AvalancheType type, int cell_id,
                      std::uint64_t parent_index) {
    if (time_ns < config_.history_start_ns ||
        time_ns > config_.window_end_ns) {
      return;
    }
    queue.push(Candidate{time_ns, type, cell_id, parent_index, sequence++});
  };

  for (const auto& photon : arrivals) {
    if (photon.sensor_id != config_.sensor_id) continue;
    const int cell = photon.has_local_position
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
        (config_.window_end_ns - config_.history_start_ns) * 1.0e-9;
    std::poisson_distribution<std::size_t> n_dark(
        config_.dark_count_rate_hz * duration_s);
    result.n_dark_candidates = n_dark(rng);
    std::uniform_real_distribution<double> dark_time(
        config_.history_start_ns, config_.window_end_ns);
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

    if (!never_fired && (dt <= config_.dead_time_ns || dt < 0.0)) {
      ++result.n_rejected_dead_or_recovery;
      continue;
    }

    // Recovery modelling (issue #1066, ARU-SIPM-RECOVERY-LAW-001).
    // Two distinct quantities: the probability the cell fires (trigger
    // acceptance) and the amplitude of that fire (gain recovery).  Both are
    // functions of the elapsed time since the last fire, via the single
    // charge-recovery time constant, but the model-form assumption that they
    // are equal is now explicit and replaceable.
    const double r_dt = never_fired
                            ? 1.0
                            : Clamp01(1.0 - std::exp(
                                -dt / config_.recovery_time_ns));
    const double trigger_recovery = EvaluateTriggerRecovery(r_dt);
    if (unit(rng) > trigger_recovery) {
      ++result.n_rejected_dead_or_recovery;
      continue;
    }

    const double gain_recovery = EvaluateGainRecovery(r_dt);
    std::normal_distribution<double> gain(
        config_.gain_mean_pe,
        config_.gain_mean_pe * config_.gain_sigma_fraction);
    const double amplitude = std::max(0.0, gain(rng)) * gain_recovery;

    Avalanche avalanche;
    avalanche.index = result.avalanches.size();
    avalanche.type = candidate.type;
    avalanche.parent_index = candidate.parent_index;
    avalanche.cell_id = candidate.cell_id;
    avalanche.cell_x = candidate.cell_id % config_.cells_x;
    avalanche.cell_y = candidate.cell_id / config_.cells_x;
    avalanche.time_ns = candidate.time_ns;
    avalanche.amplitude_pe = amplitude;
    avalanche.recovery_fraction = r_dt;
    avalanche.delta_since_last_fire_ns = dt;
    result.avalanches.push_back(avalanche);

    last_fire[static_cast<std::size_t>(candidate.cell_id)] = candidate.time_ns;
    const std::uint64_t this_index = avalanche.index;

    if (config_.enable_prompt_crosstalk &&
        config_.prompt_crosstalk_probability > 0.0) {
      const double lambda =
          -std::log1p(-config_.prompt_crosstalk_probability);
      std::poisson_distribution<int> multiplicity(lambda * r_dt);
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
        unit(rng) < config_.delayed_crosstalk_probability * r_dt) {
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
        unit(rng) < config_.afterpulse_fast_probability * r_dt) {
      std::exponential_distribution<double> delay(
          1.0 / config_.afterpulse_fast_tau_ns);
      schedule(candidate.time_ns + delay(rng),
               AvalancheType::AfterpulseFast,
               candidate.cell_id, this_index);
    }
    if (config_.enable_afterpulsing &&
        unit(rng) < config_.afterpulse_slow_probability * r_dt) {
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

std::vector<double> ResponseSimulator::make_impulse_kernel(
    std::size_t n_samples, double dt_ns) const {
  std::vector<double> h(n_samples, 0.0);
  if (n_samples == 0) return h;

  const bool has_measured = !config_.measured_impulse_t_ns.empty();
  if (has_measured) {
    for (std::size_t i = 0; i < n_samples; ++i) {
      const double t = static_cast<double>(i) * dt_ns;
      h[i] = InterpLinear(config_.measured_impulse_t_ns,
                          config_.measured_impulse_amplitude, t);
    }
  } else if (config_.impulse_model == "IDEAL_DELTA_TEST_ONLY") {
    h[0] = 1.0;
    return h;
  } else {
    for (std::size_t i = 0; i < n_samples; ++i) {
      const double t = static_cast<double>(i) * dt_ns;
      h[i] = std::exp(-t / config_.pulse_decay_ns) -
             std::exp(-t / config_.pulse_rise_ns);
    }
    const int extra = std::max(0, config_.shaper_integrator_stages - 1);
    if (extra > 0) {
      std::vector<double> rc(n_samples, 0.0);
      for (std::size_t i = 0; i < n_samples; ++i) {
        rc[i] = std::exp(-static_cast<double>(i) * dt_ns /
                         config_.shaper_extra_stage_tau_ns);
      }
      for (int stage = 0; stage < extra; ++stage) {
        std::vector<double> conv(n_samples, 0.0);
        for (std::size_t k = 0; k < n_samples; ++k) {
          double acc = 0.0;
          for (std::size_t j = 0; j <= k; ++j) {
            acc += h[j] * rc[k - j];
          }
          conv[k] = acc;
        }
        h.swap(conv);
      }
    }
  }

  double peak = 0.0;
  for (double v : h) peak = std::max(peak, v);
  if (!(peak > 0.0)) {
    throw std::invalid_argument(
        "impulse kernel is degenerate: refusing ideal-delta fallback");
  }
  for (double& v : h) v /= peak;
  return h;
}

Waveform ResponseSimulator::make_waveform(
    const std::vector<Avalanche>& avalanches,
    std::uint64_t waveform_seed) const {
  Waveform waveform;
  const std::size_t n_samples = waveform_sample_count();
  waveform.time_ns.resize(n_samples);
  waveform.signal_pe.assign(n_samples, 0.0);
  waveform.analog_pe.assign(n_samples, 0.0);
  waveform.adc.assign(n_samples, 0);

  const double dt = config_.sample_dt_ns;
  const std::vector<double>& h = waveform_kernel_;
  if (h.size() != impulse_kernel_sample_count()) {
    throw std::logic_error(
        "cached waveform kernel does not match history-complete support");
  }

  for (std::size_t i = 0; i < n_samples; ++i) {
    waveform.time_ns[i] =
        config_.window_start_ns + static_cast<double>(i) * dt;
  }

  // Convolve the delta-train of avalanches with the impulse kernel.  Each
  // avalanche contributes amplitude * h(t_i - t_a) at every recorded sample,
  // with h evaluated at the continuous elapsed time (t_i - t_a) by linear
  // interpolation between kernel samples.  This removes the sub-grid phase
  // error of nearest-bin placement: an avalanche at a non-integer sample
  // offset now lands at its true fractional delay instead of being snapped
  // to the nearest sample (issue #1065).  The cached kernel is the exact same
  // history-complete object whose canonical identity is emitted by
  // run_metadata(), so provenance cannot silently hash a reconstructed copy.
  for (const auto& avalanche : avalanches) {
    const double amplitude = avalanche.amplitude_pe;
    if (amplitude == 0.0) continue;
    if (h.empty()) break;
    // First sample index whose time is >= the avalanche time (causal kernel;
    // avalanches before window_start still contribute their tail via the
    // prehistory-extended kernel).
    const double offset = (avalanche.time_ns - config_.window_start_ns) / dt;
    const std::size_t i0 = offset > 0.0
        ? std::min(n_samples, static_cast<std::size_t>(std::ceil(offset)))
        : 0U;
    for (std::size_t i = i0; i < n_samples; ++i) {
      // Continuous elapsed time since the avalanche, in sample units.
      const double pos = static_cast<double>(i) - offset;
      const std::size_t k = static_cast<std::size_t>(pos);
      if (k >= h.size()) continue;  // finite kernel: tail beyond support
      const double frac = pos - static_cast<double>(k);
      const double h0 = h[k];
      const double h1 = (k + 1 < h.size()) ? h[k + 1] : h0;
      waveform.signal_pe[i] += amplitude * (h0 + frac * (h1 - h0));
    }
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

RunMetadata ResponseSimulator::run_metadata() const {
  RunMetadata metadata;
  metadata.device = config_.device_provenance;
  metadata.electronics = config_.electronics_provenance;
  metadata.electronics.integrator_stages = config_.shaper_integrator_stages;
  metadata.impulse_model = config_.impulse_model;

  if (config_.impulse_model == "MEASURED") {
    // `impulse_model` identifies the numerical sampled-kernel family only.
    // It is not sufficient evidence that the kernel is a measured/calibrated
    // electronics response.  Until source bytes and calibration/resampling
    // validation are verified by an explicit promotion contract, fail closed
    // in the serialized provenance state rather than advertising an arbitrary
    // custom vector as MEASURED.
    metadata.electronics.impulse_response_status = "CUSTOM_UNVALIDATED";
  } else if (config_.impulse_model == "IDEAL_DELTA_TEST_ONLY") {
    metadata.electronics.impulse_response_status = "IDEAL_DELTA_TEST_ONLY";
  }

  // Never trust a caller-supplied effective-kernel hash.  When waveform
  // generation is active, hash the exact cached history-complete kernel object
  // consumed by make_waveform().  When no waveform is generated there is no
  // consumed runtime kernel, so clear the field rather than advertising an
  // unexecuted or caller-provided identity.
  metadata.electronics.effective_kernel_hash.clear();
  if (config_.generate_waveform) {
    metadata.electronics.effective_kernel_hash =
        CanonicalEffectiveKernelHash(config_.sample_dt_ns, waveform_kernel_);
  }

  metadata.pde_curve = config_.pde_curve;
  metadata.recovery_time_ns = config_.recovery_time_ns;
  metadata.dark_count_rate_hz = config_.dark_count_rate_hz;
  metadata.prompt_crosstalk_probability =
      config_.prompt_crosstalk_probability;
  metadata.delayed_crosstalk_probability =
      config_.delayed_crosstalk_probability;
  metadata.afterpulse_fast_probability =
      config_.afterpulse_fast_probability;
  metadata.afterpulse_slow_probability =
      config_.afterpulse_slow_probability;
  metadata.adc_bits = config_.adc_bits;
  metadata.adc_lsb_pe = config_.adc_lsb_pe;
  metadata.baseline_adc = config_.baseline_adc;
  metadata.sample_dt_ns = config_.sample_dt_ns;
  metadata.window_start_ns = config_.window_start_ns;
  metadata.window_end_ns = config_.window_end_ns;
  metadata.history_start_ns = config_.history_start_ns;
  metadata.trigger_recovery_model = config_.trigger_recovery_model;
  metadata.gain_recovery_model = config_.gain_recovery_model;
  return metadata;
}

}  // namespace ccb::sipm
