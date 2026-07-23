#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace ccb::sipm {

enum class AvalancheType {
  Photon,
  Dark,
  PromptCrosstalk,
  DelayedCrosstalk,
  AfterpulseFast,
  AfterpulseSlow
};

inline const char* ToString(AvalancheType type) {
  switch (type) {
    case AvalancheType::Photon: return "photon";
    case AvalancheType::Dark: return "dark";
    case AvalancheType::PromptCrosstalk: return "prompt_crosstalk";
    case AvalancheType::DelayedCrosstalk: return "delayed_crosstalk";
    case AvalancheType::AfterpulseFast: return "afterpulse_fast";
    case AvalancheType::AfterpulseSlow: return "afterpulse_slow";
  }
  return "unknown";
}

struct PhotonArrival {
  std::uint64_t photon_id = 0;
  int sensor_id = 0;
  double time_ns = 0.0;
  double wavelength_nm = 0.0;
  double x_mm = 0.0;
  double y_mm = 0.0;
  bool has_local_position = true;
};

struct Avalanche {
  std::uint64_t index = 0;
  AvalancheType type = AvalancheType::Photon;
  std::uint64_t parent_index = std::numeric_limits<std::uint64_t>::max();
  int cell_id = -1;
  int cell_x = -1;
  int cell_y = -1;
  double time_ns = 0.0;
  double amplitude_pe = 0.0;
  double recovery_fraction = 0.0;
  double delta_since_last_fire_ns = std::numeric_limits<double>::infinity();
};

struct Waveform {
  std::vector<double> time_ns;
  std::vector<double> signal_pe;
  std::vector<double> analog_pe;
  std::vector<int> adc;
};

struct EventResult {
  std::uint64_t event_id = 0;
  int sensor_id = 0;
  std::size_t n_incident_photons = 0;
  std::size_t n_outside_active_area = 0;
  std::size_t n_primary_candidates = 0;
  std::size_t n_dark_candidates = 0;
  std::size_t n_rejected_dead_or_recovery = 0;
  std::size_t n_candidates_processed = 0;
  bool candidate_limit_reached = false;
  std::vector<Avalanche> avalanches;
  Waveform waveform;

  std::size_t count(AvalancheType type) const {
    std::size_t n = 0;
    for (const auto& a : avalanches) {
      if (a.type == type) ++n;
    }
    return n;
  }
};

}  // namespace ccb::sipm
