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

 private:
  ModelConfig config_;

  int cell_from_position(double x_mm, double y_mm) const;
  Waveform make_waveform(const std::vector<Avalanche>& avalanches,
                         std::uint64_t waveform_seed) const;
};

}  // namespace ccb::sipm
