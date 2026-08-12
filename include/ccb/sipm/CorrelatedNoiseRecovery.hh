#pragma once

#include <string>

namespace ccb::sipm {

// Explicit parent-avalanche recovery laws for correlated-noise generation.
// These are model-form identifiers, not detector-truth labels.
inline constexpr const char* kParentRecoveryRawRechargeLegacy =
    "RAW_RECHARGE_LEGACY";
inline constexpr const char* kParentRecoveryGainCoupledHypothesis =
    "GAIN_COUPLED_HYPOTHESIS";
inline constexpr const char* kParentRecoveryUnsuppressedControl =
    "UNSUPPRESSED_CONTROL";

bool IsKnownCorrelatedNoiseParentRecoveryModel(const std::string& model);

double EvaluateCorrelatedNoiseParentRecovery(
    const std::string& model,
    double raw_recovery,
    double gain_recovery);

}  // namespace ccb::sipm
