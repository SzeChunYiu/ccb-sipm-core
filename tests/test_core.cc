#include "ccb/sipm/Config.hh"
#include "ccb/sipm/ResponseSimulator.hh"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using ccb::sipm::ModelConfig;
using ccb::sipm::PhotonArrival;
using ccb::sipm::ResponseSimulator;
using ccb::sipm::RunMetadata;

namespace {

int g_failures = 0;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
  }
}

ModelConfig UnitConfig() {
  auto c = ModelConfig::RepresentativeS13360_3050CS();
  c.cells_x = 4;
  c.cells_y = 4;
  c.active_width_mm = 4.0;
  c.active_height_mm = 4.0;
  c.pde_curve = {{300.0, 1.0}, {700.0, 1.0}};
  c.pde_scale = 1.0;
  c.coupling_efficiency = 1.0;
  c.enable_dark_counts = false;
  c.enable_prompt_crosstalk = false;
  c.enable_delayed_crosstalk = false;
  c.enable_afterpulsing = false;
  c.gain_sigma_fraction = 0.0;
  c.sptr_sigma_ns = 0.0;
  c.electronics_noise_sigma_pe = 0.0;
  c.window_start_ns = 0.0;
  c.window_end_ns = 300.0;
  c.sample_dt_ns = 1.0;
  c.validate();
  return c;
}

PhotonArrival Hit(double time, double x, double y) {
  PhotonArrival p;
  p.sensor_id = 0;
  p.time_ns = time;
  p.wavelength_nm = 450.0;
  p.x_mm = x;
  p.y_mm = y;
  return p;
}

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

// Argmax of a non-empty vector.
std::size_t ArgMax(const std::vector<double>& v) {
  std::size_t best = 0;
  for (std::size_t i = 1; i < v.size(); ++i) {
    if (v[i] > v[best]) best = i;
  }
  return best;
}

}  // namespace

int main() {
  // ----- Existing core tests (PDE, recovery, determinism, validation) -----
  {
    auto c = UnitConfig();
    ResponseSimulator sim(c);
    Require(std::abs(sim.photon_detection_efficiency(500.0) - 1.0) < 1e-12,
            "PDE interpolation");
    Require(sim.photon_detection_efficiency(250.0) == 0.0,
            "PDE outside range must fail closed");
  }

  {
    auto c = UnitConfig();
    ResponseSimulator sim(c);
    const auto r = sim.simulate({Hit(10.0, 0.0, 0.0)}, 42, 7);
    Require(r.n_primary_candidates == 1, "one primary candidate");
    Require(r.avalanches.size() == 1, "one avalanche");
    Require(std::abs(r.avalanches[0].amplitude_pe - 1.0) < 1e-12,
            "fully charged first cell");
  }

  {
    auto c = UnitConfig();
    ResponseSimulator sim(c);
    const auto r = sim.simulate(
        {Hit(10.0, 0.0, 0.0), Hit(10.0, 0.0, 0.0)}, 42, 8);
    Require(r.avalanches.size() == 1,
            "simultaneous photons in one cell saturate");
    Require(r.n_rejected_dead_or_recovery >= 1,
            "second simultaneous candidate rejected");
  }

  {
    auto c = UnitConfig();
    ResponseSimulator sim(c);
    const auto r = sim.simulate(
        {Hit(10.0, 0.0, 0.0), Hit(200.0, 0.0, 0.0)}, 42, 9);
    Require(r.avalanches.size() == 2, "recovered cell fires twice");
    Require(r.avalanches[1].recovery_fraction > 0.99,
            "long-delay recovery approaches one");
  }

  {
    auto c = UnitConfig();
    ResponseSimulator sim(c);
    const std::vector<PhotonArrival> hits = {
        Hit(10.0, -1.0, -1.0), Hit(20.0, 1.0, 1.0)};
    const auto a = sim.simulate(hits, 123456, 10);
    const auto b = sim.simulate(hits, 123456, 10);
    Require(a.avalanches.size() == b.avalanches.size(),
            "deterministic avalanche size");
    Require(a.waveform.adc == b.waveform.adc,
            "deterministic waveform");
    for (std::size_t i = 0; i < a.avalanches.size(); ++i) {
      Require(a.avalanches[i].time_ns == b.avalanches[i].time_ns,
              "deterministic avalanche times");
      Require(a.avalanches[i].cell_id == b.avalanches[i].cell_id,
              "deterministic cell IDs");
    }
  }

  {
    auto c = UnitConfig();
    ResponseSimulator sim(c);
    const auto r = sim.simulate({}, 1, 1);
    Require(r.avalanches.empty(), "empty event has no avalanches");
    Require(!r.waveform.adc.empty(), "waveform generated");
    for (int code : r.waveform.adc) {
      Require(code == static_cast<int>(c.baseline_adc),
              "noise-free empty waveform is baseline");
    }
  }

  {
    auto c = UnitConfig();
    c.prompt_crosstalk_probability = 1.0;
    bool threw = false;
    try {
      c.validate();
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    Require(threw, "probability domain validation");
  }

  // ===== TASK A: device-profile provenance + PDE-source match =====

  // A1: RepresentativeS13360_3050CS() carries the required provenance fields.
  {
    const auto c = ModelConfig::RepresentativeS13360_3050CS();
    const auto& p = c.device_provenance;
    Require(p.device_name == "Hamamatsu S13360-3050CS",
            "provenance device_name");
    Require(p.primary_source_id == "SRC-HAMA-001",
            "provenance primary source id");
    Require(p.primary_source_type == "MANUFACTURER",
            "provenance primary source type");
    Require(!p.primary_source_url.empty(),
            "provenance primary source url present");
    Require(p.primary_source_retrieved_date == "2026-07-23",
            "provenance primary retrieved date");
    Require(p.secondary_source_id == "SRC-HAMA-002",
            "provenance secondary source id");
    Require(p.secondary_source_retrieved_date == "2026-07-23",
            "provenance secondary retrieved date");
    Require(p.calibration_status ==
            "MANUFACTURER_REPRESENTATIVE_NOT_CALIBRATED",
            "provenance calibration status stays MANUFACTURER_REPRESENTATIVE");
    Require(std::abs(p.overvoltage_V - 3.0) < 1e-12,
            "provenance overvoltage");
    Require(std::abs(p.temperature_C - 25.0) < 1e-12,
            "provenance temperature");
    Require(!p.covered_parameters.empty(),
            "provenance covered_parameters non-empty");
    Require(Contains(p.covered_parameters, "pde_curve"),
            "provenance covers pde_curve");
    Require(Contains(p.covered_parameters, "dark_count_rate_hz"),
            "provenance covers DCR");
    Require(Contains(p.covered_parameters, "recovery_time_ns"),
            "provenance covers recovery");
    Require(Contains(p.covered_parameters, "prompt_crosstalk_probability"),
            "provenance covers prompt crosstalk");
    Require(Contains(p.covered_parameters, "afterpulse_fast_probability"),
            "provenance covers afterpulse");
  }

  // A2: PDE curve matches the source (exact values from the device profile).
  {
    const auto c = ModelConfig::RepresentativeS13360_3050CS();
    Require(c.pde_curve.size() == 10, "PDE curve has 10 points");
    const std::vector<double> wl = {300, 350, 400, 450, 476,
                                    500, 550, 600, 650, 700};
    const std::vector<double> pv = {0.06, 0.18, 0.33, 0.40, 0.38,
                                    0.35, 0.27, 0.18, 0.11, 0.06};
    for (std::size_t i = 0; i < wl.size(); ++i) {
      Require(std::abs(c.pde_curve[i].wavelength_nm - wl[i]) < 1e-12,
              "PDE wavelength matches source");
      Require(std::abs(c.pde_curve[i].pde - pv[i]) < 1e-12,
              "PDE value matches source");
    }
    // Defaults recorded with the same provenance must match the JSON values.
    Require(std::abs(c.recovery_time_ns - 30.0) < 1e-12,
            "representative recovery time");
    Require(std::abs(c.dark_count_rate_hz - 500000.0) < 1e-9,
            "representative DCR");
  }

  // A3: run_metadata().render_json() emits the provenance + PDE curve.
  {
    const auto c = ModelConfig::RepresentativeS13360_3050CS();
    const ResponseSimulator sim(c);
    const RunMetadata md = sim.run_metadata();
    const std::string j = md.render_json();
    Require(Contains(j, "SRC-HAMA-001"), "metadata has primary source id");
    Require(Contains(j, "SRC-HAMA-002"), "metadata has secondary source id");
    Require(Contains(j, "2026-07-23"), "metadata has retrieved date");
    Require(Contains(j, "MANUFACTURER_REPRESENTATIVE_NOT_CALIBRATED"),
            "metadata has calibration status");
    Require(Contains(j, "ASSUMPTION_GENERIC_CRRC_NOT_MEASURED"),
            "metadata has generic-electronics status");
    Require(Contains(j, "pde_curve"), "metadata has pde_curve");
    Require(Contains(j, "[450, 0.4]"), "metadata pde_curve matches source");
    Require(Contains(j, "[476, 0.38]"), "metadata pde_curve peak point");
    Require(Contains(j, "dark_count_rate_hz"),
            "metadata records DCR default");
    Require(Contains(j, "recovery_time_ns"),
            "metadata records recovery default");
  }

  // ===== TASK B: generic electronics (CR-RC(-RC) shaper + ADC) =====

  // B1: stages=1 impulse is causal, finite, peak-normalised.
  {
    auto c = UnitConfig();
    ResponseSimulator sim(c);
    const auto r = sim.simulate({Hit(10.0, 0.0, 0.0)}, 42, 100);
    Require(r.avalanches.size() == 1, "B1 single avalanche");
    const auto& sig = r.waveform.signal_pe;
    const std::size_t peak_i = ArgMax(sig);
    // Causal: every sample strictly before the avalanche time is zero.
    for (std::size_t i = 0; i < sig.size(); ++i) {
      if (r.waveform.time_ns[i] < 10.0 - 1e-9) {
        Require(std::abs(sig[i]) < 1e-12, "B1 causal (no signal before fire)");
      }
    }
    // Peak-normalised: max signal ~= 1.0 (unit-amplitude avalanche).
    Require(std::abs(sig[peak_i] - 1.0) < 1e-9, "B1 peak normalised to 1.0");
    // Finite: tail returns to ~0 by the end of the window.
    Require(std::abs(sig.back()) < 1e-3, "B1 impulse finite (tail decays)");
    // Peak occurs after the fire time.
    Require(r.waveform.time_ns[peak_i] > 10.0,
            "B1 peak time strictly after fire");
  }

  // B2: stages=2 (CR-RC-RC) impulse is broader: peak time strictly later than
  // stages=1 for the same fire time.
  {
    auto c1 = UnitConfig();
    auto c2 = UnitConfig();
    c2.shaper_integrator_stages = 2;
    c2.shaper_extra_stage_tau_ns = 25.0;  // = pulse_decay default
    c1.validate();
    c2.validate();
    const ResponseSimulator s1(c1), s2(c2);
    const auto r1 = s1.simulate({Hit(20.0, 0.0, 0.0)}, 1, 1);
    const auto r2 = s2.simulate({Hit(20.0, 0.0, 0.0)}, 1, 1);
    const std::size_t p1 = ArgMax(r1.waveform.signal_pe);
    const std::size_t p2 = ArgMax(r2.waveform.signal_pe);
    Require(r2.waveform.signal_pe[p2] >= 0.99,
            "B2 stages=2 still peak-normalised");
    Require(p2 > p1, "B2 stages=2 peak later than stages=1 (broader impulse)");
    // Causality preserved by the multi-stage shaper.
    for (std::size_t i = 0; i < r2.waveform.signal_pe.size(); ++i) {
      if (r2.waveform.time_ns[i] < 20.0 - 1e-9) {
        Require(std::abs(r2.waveform.signal_pe[i]) < 1e-12,
                "B2 stages=2 causal");
      }
    }
  }

  // B3: ADC quantisation stays within [0, 2^bits - 1] for a saturating pulse.
  {
    auto c = UnitConfig();
    c.gain_mean_pe = 5000.0;   // huge amplitude -> ADC clips at top
    c.adc_bits = 12;
    c.adc_lsb_pe = 0.01;
    c.baseline_adc = 200.0;
    c.validate();
    const ResponseSimulator sim(c);
    const auto r = sim.simulate({Hit(10.0, 0.0, 0.0)}, 1, 1);
    Require(!r.waveform.adc.empty(), "B3 waveform produced");
    const int max_adc = (1 << c.adc_bits) - 1;
    int lo = std::numeric_limits<int>::max();
    int hi = std::numeric_limits<int>::min();
    for (int code : r.waveform.adc) {
      lo = std::min(lo, code);
      hi = std::max(hi, code);
    }
    Require(hi == max_adc, "B3 saturating pulse hits ADC ceiling");
    Require(lo >= 0, "B3 ADC never below 0");
    Require(hi <= max_adc, "B3 ADC never above 2^bits - 1");
  }

  // B4: measured-impulse hook replaces the analytical shaper, and the metadata
  // status flips to MEASURED.
  {
    auto c = UnitConfig();
    c.measured_impulse_t_ns = {0.0, 1.0, 2.0, 3.0, 4.0};
    c.measured_impulse_amplitude = {0.0, 0.5, 1.0, 0.5, 0.0};
    c.validate();
    const ResponseSimulator sim(c);
    const auto r = sim.simulate({Hit(10.0, 0.0, 0.0)}, 1, 1);
    const auto& sig = r.waveform.signal_pe;
    // base sample = round((10 - 0)/1) = 10.  Peak of measured kernel at t=2 ->
    // sample 12.
    Require(std::abs(sig[12] - 1.0) < 1e-9, "B4 measured peak at sample 12");
    Require(std::abs(sig[11] - 0.5) < 1e-9, "B4 measured rising edge");
    Require(std::abs(sig[13] - 0.5) < 1e-9, "B4 measured falling edge");
    Require(std::abs(sig[14]) < 1e-9, "B4 measured finite tail");
    Require(std::abs(sig[9]) < 1e-12, "B4 measured causal before fire");
    const RunMetadata md = sim.run_metadata();
    Require(md.electronics.impulse_response_status == "MEASURED",
            "B4 metadata status flips to MEASURED");
  }

  // B5: environment overrides for window / shaper / ADC bits.
  {
    auto c = ModelConfig::RepresentativeS13360_3050CS();
    c.enable_dark_counts = false;
    c.enable_prompt_crosstalk = false;
    c.enable_delayed_crosstalk = false;
    c.enable_afterpulsing = false;
    c.pde_curve = {{300.0, 1.0}, {700.0, 1.0}};
    c.gain_sigma_fraction = 0.0;
    c.sptr_sigma_ns = 0.0;
    c.electronics_noise_sigma_pe = 0.0;
    c.sample_dt_ns = 1.0;
    c.window_start_ns = 0.0;
    c.window_end_ns = 100.0;
    c.validate();

    setenv("CCB_SIPM_ADC_BITS", "14", 1);
    setenv("CCB_SIPM_WINDOW_END_NS", "200", 1);
    setenv("CCB_SIPM_SHAPER_STAGES", "2", 1);
    const int n = ModelConfig::ApplyEnvironmentOverrides(c);
    c.validate();
    unsetenv("CCB_SIPM_ADC_BITS");
    unsetenv("CCB_SIPM_WINDOW_END_NS");
    unsetenv("CCB_SIPM_SHAPER_STAGES");
    Require(n == 3, "B5 three env keys applied");
    Require(c.adc_bits == 14, "B5 env override adc_bits");
    Require(std::abs(c.window_end_ns - 200.0) < 1e-12, "B5 env override window");
    Require(c.shaper_integrator_stages == 2, "B5 env override shaper stages");
  }

  // ===== TASK C (issue #1096): pre-window avalanche tails =====
  // A photon arriving before window_start_ns but after history_start_ns must
  // still be scheduled so its analog tail contributes to the recorded window.
  {
    auto c = UnitConfig();
    c.window_start_ns = -20.0;
    c.window_end_ns = 250.0;
    c.history_start_ns = -200.0;  // pre-window scheduling boundary
    c.validate();
    const ResponseSimulator sim(c);
    // Photon at t=-21 ns: before the window but inside the history window.
    const auto r = sim.simulate({Hit(-21.0, 0.0, 0.0)}, 42, 110);
    Require(r.avalanches.size() == 1, "C1 pre-window photon scheduled");
    const auto& sig = r.waveform.signal_pe;
    // A generic CR-RC impulse peak-normalised at ~(rise+decay): the tail at
    // window start (t=-20) is still ~exp(-t/25) ~ 0.96 of the peak, so the
    // recorded in-window samples must be non-negligible.
    bool has_tail = false;
    for (double v : sig) {
      if (v > 0.5) has_tail = true;
    }
    Require(has_tail, "C2 pre-window tail recorded in window");
  }

  // C3: candidates earlier than history_start_ns are still rejected.
  {
    auto c = UnitConfig();
    c.window_start_ns = -20.0;
    c.window_end_ns = 250.0;
    c.history_start_ns = -200.0;
    c.validate();
    const ResponseSimulator sim(c);
    const auto r = sim.simulate({Hit(-300.0, 0.0, 0.0)}, 42, 111);
    Require(r.avalanches.empty(), "C3 candidate before history rejected");
  }

  // C4: run_metadata records history_start_ns.
  {
    auto c = UnitConfig();
    c.window_start_ns = -20.0;
    c.window_end_ns = 250.0;
    c.history_start_ns = -200.0;
    c.validate();
    const ResponseSimulator sim(c);
    const RunMetadata md = sim.run_metadata();
    Require(std::abs(md.history_start_ns - (-200.0)) < 1e-12,
            "C4 metadata records history_start_ns");
    const std::string j = md.render_json();
    Require(Contains(j, "history_start_ns"), "C4 metadata json has history_start_ns");
  }

  // C5: env override for history_start_ns.
  {
    auto c = UnitConfig();
    setenv("CCB_SIPM_HISTORY_START_NS", "-150", 1);
    const int n = ModelConfig::ApplyEnvironmentOverrides(c);
    unsetenv("CCB_SIPM_HISTORY_START_NS");
    Require(n == 1, "C5 history env key applied");
    Require(std::abs(c.history_start_ns - (-150.0)) < 1e-12,
            "C5 env override history_start_ns");
  }

  if (g_failures == 0) {
    std::cout << "All ccb_sipm_core tests passed\n";
    return 0;
  }
  std::cerr << g_failures << " test failure(s)\n";
  return 1;
}
