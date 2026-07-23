#include "ccb/sipm/Config.hh"
#include "ccb/sipm/ResponseSimulator.hh"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

using ccb::sipm::ModelConfig;
using ccb::sipm::PhotonArrival;
using ccb::sipm::ResponseSimulator;

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
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

}  // namespace

int main() {
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

  std::cout << "All ccb_sipm_core tests passed\n";
  return 0;
}
