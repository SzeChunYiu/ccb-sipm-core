#pragma once

#include "ccb/sipm/Types.hh"

#include <optional>

class G4Step;
class G4VPhysicalVolume;

namespace ccb::sipm {

class Geant4BoundaryCollector {
 public:
  static std::optional<PhotonArrival> FromStep(
      const G4Step& step,
      int sensor_id,
      const G4VPhysicalVolume* expected_sensor_volume);
};

}  // namespace ccb::sipm
