#include "ccb/sipm/Geant4BoundaryCollector.hh"

#include "G4OpticalPhoton.hh"
#include "G4PhysicalConstants.hh"
#include "G4Step.hh"
#include "G4StepPoint.hh"
#include "G4SystemOfUnits.hh"
#include "G4TouchableHistory.hh"
#include "G4Track.hh"
#include "G4VPhysicalVolume.hh"

#include <cmath>

namespace ccb::sipm {

std::optional<PhotonArrival> Geant4BoundaryCollector::FromStep(
    const G4Step& step,
    int sensor_id,
    const G4VPhysicalVolume* expected_sensor_volume) {
  const G4Track* track = step.GetTrack();
  if (track == nullptr ||
      track->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition()) {
    return std::nullopt;
  }

  const G4StepPoint* post = step.GetPostStepPoint();
  if (post == nullptr || post->GetStepStatus() != fGeomBoundary ||
      post->GetPhysicalVolume() != expected_sensor_volume) {
    return std::nullopt;
  }

  const double energy = track->GetTotalEnergy();
  if (!(energy > 0.0)) return std::nullopt;

  PhotonArrival arrival;
  arrival.photon_id = static_cast<std::uint64_t>(track->GetTrackID());
  arrival.sensor_id = sensor_id;
  arrival.time_ns = track->GetGlobalTime() / ns;
  arrival.wavelength_nm =
      (h_Planck * c_light / energy) / nm;

  const auto touchable = post->GetTouchableHandle();
  if (touchable && touchable->GetHistory()) {
    const G4ThreeVector local =
        touchable->GetHistory()->GetTopTransform().TransformPoint(
            post->GetPosition());
    arrival.x_mm = local.x() / mm;
    arrival.y_mm = local.y() / mm;
    arrival.has_local_position = true;
  } else {
    arrival.has_local_position = false;
  }
  return arrival;
}

}  // namespace ccb::sipm
