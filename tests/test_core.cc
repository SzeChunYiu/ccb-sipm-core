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

  // B4: sampled-impulse hook replaces the analytical shaper numerically, while
  // metadata stays non-authoritative until measured-calibration provenance is
  // independently verified.
  {
    auto c = UnitConfig();
    c.measured_impulse_t_ns = {0.0, 1.0, 2.0, 3.0, 4.0};
    c.measured_impulse_amplitude = {0.0, 0.5, 1.0, 0.5, 0.0};
    c.validate();
    const ResponseSimulator sim(c);
    const auto r = sim.simulate({Hit(10.0, 0.0, 0.0)}, 1, 1);
    const auto& sig = r.waveform.signal_pe;
    // base sample = round((10 - 0)/1) = 10.  Peak of sampled kernel at t=2 ->
    // sample 12.
    Require(std::abs(sig[12] - 1.0) < 1e-9, "B4 sampled peak at sample 12");
    Require(std::abs(sig[11] - 0.5) < 1e-9, "B4 sampled rising edge");
    Require(std::abs(sig[13] - 0.5) < 1e-9, "B4 sampled falling edge");
    Require(std::abs(sig[14]) < 1e-9, "B4 sampled finite tail");
    Require(std::abs(sig[9]) < 1e-12, "B4 sampled causal before fire");
    const RunMetadata md = sim.run_metadata();
    Require(md.electronics.impulse_response_status == "CUSTOM_UNVALIDATED",
            "B4 unbound sampled impulse remains non-authoritative");
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

  // ===== TASK D (issue #1065): continuous-phase (fractional-delay) kernels ====
  // The old nearest-bin placement snapped each avalanche to the nearest
  // sample (sub-grid phase error up to dt/2).  The new code evaluates
  // h(t_i - t_a) at the continuous elapsed time via linear interpolation
  // between kernel samples.

  // D1: Integer-sample offset reproduces the old exact-on-grid peak shape.
  {
    auto c = UnitConfig();
    c.measured_impulse_t_ns = {0.0, 1.0, 2.0, 3.0, 4.0};
    c.measured_impulse_amplitude = {0.0, 0.5, 1.0, 0.5, 0.0};
    c.validate();
    const ResponseSimulator sim(c);
    const auto r = sim.simulate({Hit(10.0, 0.0, 0.0)}, 1, 200);
    const auto& sig = r.waveform.signal_pe;
    // base = floor(10/1) = 10, so the triangle kernel sits at samples 10-14.
    Require(std::abs(sig[12] - 1.0) < 1e-9,
            "D1 integer-offset peak at sample 12");
    Require(std::abs(sig[11] - 0.5) < 1e-9,
            "D1 integer-offset rising edge");
    Require(std::abs(sig[13] - 0.5) < 1e-9,
            "D1 integer-offset falling edge");
    Require(std::abs(sig[10]) < 1e-12,
            "D1 integer-offset causal before fire");
  }

  // D2: Fractional-sample offset spreads energy across adjacent samples
  // (verifies linear interpolation of h at continuous elapsed time).
  // The measured impulse is a triangle peaking at kernel sample 2 (t=2ns,
  // value 1.0):  h = {0, 0.5, 1.0, 0.5, 0}.  Photon at t=10.5 -> offset = 10.5
  // sample units, so the first causal sample is ceil(10.5) = 11.  At sample i
  // the continuous elapsed time is (i - 10.5):
  //   i=11: pos=0.5 -> h(0.5)=interp(0,0.5)=0.25
  //   i=12: pos=1.5 -> h(1.5)=interp(0.5,1)=0.75
  //   i=13: pos=2.5 -> h(2.5)=interp(1,0.5)=0.75
  //   i=14: pos=3.5 -> h(3.5)=interp(0.5,0)=0.25
  //   i=10: pos=-0.5 -> causal (0)
  //   i=15: pos=4.5 -> beyond support (0)
  {
    auto c = UnitConfig();
    c.measured_impulse_t_ns = {0.0, 1.0, 2.0, 3.0, 4.0};
    c.measured_impulse_amplitude = {0.0, 0.5, 1.0, 0.5, 0.0};
    c.validate();
    const ResponseSimulator sim(c);
    const auto r = sim.simulate({Hit(10.5, 0.0, 0.0)}, 1, 201);
    const auto& sig = r.waveform.signal_pe;
    Require(std::abs(sig[11] - 0.25) < 1e-9,
            "D2 fractional-offset first rising sample");
    Require(std::abs(sig[12] - 0.75) < 1e-9,
            "D2 fractional-offset left of true peak");
    Require(std::abs(sig[13] - 0.75) < 1e-9,
            "D2 fractional-offset right of true peak");
    Require(std::abs(sig[14] - 0.25) < 1e-9,
            "D2 fractional-offset tail sample");
    Require(std::abs(sig[10]) < 1e-12,
            "D2 fractional-offset causal before fire");
    Require(std::abs(sig[15]) < 1e-12,
            "D2 fractional-offset beyond support");
    // The kernel peak (elapsed 2.0) lands at real sample 12.5, so samples 12
    // and 13 both carry 0.75: the energy is split symmetrically around the
    // true peak instead of being snapped to a single bin.  The old nearest-bin
    // code would have collapsed the whole impulse onto one sample.
    Require(std::abs(sig[12] - sig[13]) < 1e-9,
            "D2 symmetric split around true peak (not snapped)");
  }

  // D3: The prehistory (issue #1096) tail invariant still holds with
  // fractional placement: an avalanche before window_start contributes its
  // tail into the recorded window.
  {
    auto c = UnitConfig();
    c.window_start_ns = -20.0;
    c.window_end_ns = 250.0;
    c.history_start_ns = -200.0;
    c.validate();
    const ResponseSimulator sim(c);
    const auto r = sim.simulate({Hit(-21.0, 0.0, 0.0)}, 42, 202);
    Require(r.avalanches.size() == 1, "D3 pre-window photon scheduled");
    bool has_tail = false;
    for (double v : r.waveform.signal_pe) {
      if (v > 0.5) has_tail = true;
    }
    Require(has_tail, "D3 pre-window tail recorded in window");
  }

  // ===== TASK E (issue #1066): separate trigger and gain recovery =====

  // E1: Known-answer r(dt) with EXPONENTIAL_H1_SHARED (default).
  // First fire (never_fired=true): r_dt=1.0, recovery_fraction=1.0, amplitude=1.0.
  // Second fire at dt=recovery_time_ns: r_dt=1-1/e, recovery_fraction=r_dt,
  // amplitude = gain_mean_pe * r_dt (H1: gain_recovery = r_dt).
  {
    auto c = UnitConfig();
    c.recovery_time_ns = 30.0;
    c.validate();
    const ResponseSimulator sim(c);
    // Both photons at (0,0) -> same cell (cell 10 in 4x4 grid) -> recovery applies.
    const auto r = sim.simulate(
        {Hit(10.0, 0.0, 0.0), Hit(40.0, 0.0, 0.0)}, 42, 300);
    Require(r.avalanches.size() >= 1, "E1 at least one avalanche");
    // First avalanche always fires (never_fired -> r_dt=1.0 -> trigger=1.0).
    Require(std::abs(r.avalanches[0].recovery_fraction - 1.0) < 1e-12,
            "E1 first recovery_fraction = 1.0");
    Require(std::abs(r.avalanches[0].amplitude_pe - 1.0) < 1e-12,
            "E1 first amplitude = 1.0");
    if (r.avalanches.size() >= 2) {
      const double expected = 1.0 - std::exp(-30.0 / 30.0);  // 1 - 1/e ≈ 0.6321
      Require(std::abs(r.avalanches[1].recovery_fraction - expected) < 1e-12,
              "E1 second recovery_fraction = r_dt");
      Require(std::abs(r.avalanches[1].amplitude_pe - expected) < 1e-12,
              "E1 H1 amplitude = gain_mean_pe * r_dt");
    }
  }

  // E2: FULL_RECOVERY gain_recovery_model — gain always full (1.0).
  // Trigger still uses r_dt (EXPONENTIAL), but amplitude = gain_mean_pe * 1.0.
  // recovery_fraction still reports the raw r_dt, not the gain-recovery result.
  {
    auto c = UnitConfig();
    c.recovery_time_ns = 30.0;
    c.gain_recovery_model = "FULL_RECOVERY";
    c.validate();
    const ResponseSimulator sim(c);
    const auto r = sim.simulate(
        {Hit(10.0, 0.0, 0.0), Hit(40.0, 0.0, 0.0)}, 42, 301);
    Require(r.avalanches.size() >= 1, "E2 at least one avalanche");
    if (r.avalanches.size() >= 2) {
      Require(std::abs(r.avalanches[1].amplitude_pe - 1.0) < 1e-12,
              "E2 FULL_RECOVERY amplitude = 1.0");
      const double expected = 1.0 - std::exp(-30.0 / 30.0);
      Require(std::abs(r.avalanches[1].recovery_fraction - expected) < 1e-12,
              "E2 recovery_fraction still raw r_dt");
    }
  }

  // E3: Validation fail-closed — unknown recovery-model names throw.
  {
    auto c = UnitConfig();
    c.trigger_recovery_model = "LINEAR";
    bool threw = false;
    try {
      c.validate();
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    Require(threw, "E3 unknown trigger_recovery_model throws");
  }
  {
    auto c = UnitConfig();
    c.gain_recovery_model = "QUADRATIC";
    bool threw = false;
    try {
      c.validate();
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    Require(threw, "E3 unknown gain_recovery_model throws");
  }

  // E4: run_metadata.render_json() includes recovery model fields.
  {
    const auto c = ModelConfig::RepresentativeS13360_3050CS();
    const ResponseSimulator sim(c);
    const std::string j = sim.run_metadata().render_json();
    Require(Contains(j, "trigger_recovery_model"),
            "E4 metadata has trigger_recovery_model");
    Require(Contains(j, "gain_recovery_model"),
            "E4 metadata has gain_recovery_model");
    Require(Contains(j, "EXPONENTIAL"),
            "E4 metadata shows EXPONENTIAL trigger model");
    Require(Contains(j, "EXPONENTIAL_H1_SHARED"),
            "E4 metadata shows EXPONENTIAL_H1_SHARED gain model");
  }

  // ===== TASK F (issue #1066): env-var overrides for recovery model names =====

  // F1: Valid model names are applied by ApplyEnvironmentOverrides, and the
  // resulting config still passes fail-closed validation.
  {
    auto c = UnitConfig();
    setenv("CCB_SIPM_TRIGGER_RECOVERY_MODEL", "EXPONENTIAL", 1);
    setenv("CCB_SIPM_GAIN_RECOVERY_MODEL", "FULL_RECOVERY", 1);
    const int n = ModelConfig::ApplyEnvironmentOverrides(c);
    unsetenv("CCB_SIPM_TRIGGER_RECOVERY_MODEL");
    unsetenv("CCB_SIPM_GAIN_RECOVERY_MODEL");
    Require(n == 2, "F1 two recovery-model env keys applied");
    Require(c.trigger_recovery_model == "EXPONENTIAL",
            "F1 env override trigger_recovery_model");
    Require(c.gain_recovery_model == "FULL_RECOVERY",
            "F1 env override gain_recovery_model");
    bool threw = false;
    try {
      c.validate();
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    Require(!threw, "F1 overridden config validates");
    // The override actually changes the simulator's gain behaviour: FULL_RECOVERY
    // gives a second fire full amplitude regardless of r_dt.
    const ResponseSimulator sim(c);
    const auto r = sim.simulate(
        {Hit(10.0, 0.0, 0.0), Hit(40.0, 0.0, 0.0)}, 42, 400);
    if (r.avalanches.size() >= 2) {
      Require(std::abs(r.avalanches[1].amplitude_pe - 1.0) < 1e-12,
              "F1 FULL_RECOVERY via env gives full second amplitude");
    }
  }

  // F2: A misspelt model name is applied verbatim, then fail-closed validation
  // rejects it (no silent fallback to a default law).
  {
    auto c = UnitConfig();
    setenv("CCB_SIPM_TRIGGER_RECOVERY_MODEL", "EXPPONENTIAL", 1);
    const int n = ModelConfig::ApplyEnvironmentOverrides(c);
    unsetenv("CCB_SIPM_TRIGGER_RECOVERY_MODEL");
    Require(n == 1, "F2 trigger env key applied");
    Require(c.trigger_recovery_model == "EXPPONENTIAL",
            "F2 typo applied verbatim");
    bool threw = false;
    try {
      c.validate();
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    Require(threw, "F2 typo'd model name fails closed in validate()");
  }

  if (g_failures == 0) {
    std::cout << "All ccb_sipm_core tests passed\n";
    return 0;
  }
  std::cerr << g_failures << " test failure(s)\n";
  return 1;
}
