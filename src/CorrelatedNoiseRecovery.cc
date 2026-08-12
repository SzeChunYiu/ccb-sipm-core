#include "ccb/sipm/CorrelatedNoiseRecovery.hh"

#include <cmath>
#include <stdexcept>

namespace ccb::sipm {

bool IsKnownCorrelatedNoiseParentRecoveryModel(const std::string& model) {
  return model == kParentRecoveryRawRechargeLegacy ||
         model == kParentRecoveryGainCoupledHypothesis ||
         model == kParentRecoveryUnsuppressedControl;
}

double EvaluateCorrelatedNoiseParentRecovery(
    const std::string& model,
    double raw_recovery,
    double gain_recovery) {
  if (!std::isfinite(raw_recovery) || raw_recovery < 0.0 ||
      raw_recovery > 1.0 || !std::isfinite(gain_recovery) ||
      gain_recovery < 0.0 || gain_recovery > 1.0) {
    throw std::invalid_argument(
        "correlated-noise parent recovery factors must be finite in [0,1]");
  }
  if (model == kParentRecoveryRawRechargeLegacy) {
    return raw_recovery;
  }
  if (model == kParentRecoveryGainCoupledHypothesis) {
    return gain_recovery;
  }
  if (model == kParentRecoveryUnsuppressedControl) {
    return 1.0;
  }
  throw std::invalid_argument(
      "unknown correlated-noise parent recovery model: " + model);
}

}  // namespace ccb::sipm
