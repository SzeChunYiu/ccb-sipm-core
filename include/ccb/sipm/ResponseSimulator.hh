#pragma once

#include "ccb/sipm/Config.hh"
#include "ccb/sipm/Types.hh"

#include <cstdint>
#include <vector>

namespace ccb::sipm {

class ResponseSimulator {
 public:
  explicit ResponseSimulator(ModelConfig config);

  const ModelConfig& config() const { return config_; }

  EventResult simulate(const std::vector<PhotonArrival>& arrivals,
                       std::uint64_t run_seed,
                       std::uint64_t event_id) const;

  double photon_detection_efficiency(double wavelength_nm) const;

  // Run-level metadata (device profile + electronics provenance + the model
  // parameters that produced the SiPM response).  Callers should write
  // run_metadata().render_json() into their run metadata file.
  RunMetadata run_metadata() const;

 private:
  ModelConfig config_;

  int cell_from_position(double x_mm, double y_mm) const;
  Waveform make_waveform(const std::vector<Avalanche>& avalanches,
                         std::uint64_t waveform_seed) const;
  // Build the front-end impulse response on the sample grid (peak-normalised,
  // causal: h[0] is the response at relative time 0).  Selects the measured
  // impulse when supplied, otherwise the analytical CR-RC(-RC) shaper.
  std::vector<double> make_impulse_kernel(std::size_t n_samples,
                                          double dt_ns) const;
};

}  // namespace ccb::sipm
