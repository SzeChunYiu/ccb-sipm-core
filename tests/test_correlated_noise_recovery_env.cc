#include "ccb/sipm/Config.hh"
#include "ccb/sipm/CorrelatedNoiseRecovery.hh"
#include "ccb/sipm/ResponseSimulator.hh"

#include <cassert>
#include <cstdlib>
#include <stdexcept>

int main() {
  using namespace ccb::sipm;

  assert(setenv("CCB_SIPM_PROMPT_CROSSTALK_PARENT_RECOVERY_MODEL",
                kParentRecoveryGainCoupledHypothesis, 1) == 0);
  assert(setenv("CCB_SIPM_DELAYED_CROSSTALK_PARENT_RECOVERY_MODEL",
                kParentRecoveryUnsuppressedControl, 1) == 0);
  assert(setenv("CCB_SIPM_AFTERPULSE_PARENT_RECOVERY_MODEL",
                kParentRecoveryRawRechargeLegacy, 1) == 0);

  ModelConfig config = ModelConfig::RepresentativeS13360_3050CS();
  const int applied = ModelConfig::ApplyEnvironmentOverrides(config);
  assert(applied >= 3);
  assert(config.prompt_crosstalk_parent_recovery_model ==
         kParentRecoveryGainCoupledHypothesis);
  assert(config.delayed_crosstalk_parent_recovery_model ==
         kParentRecoveryUnsuppressedControl);
  assert(config.afterpulse_parent_recovery_model ==
         kParentRecoveryRawRechargeLegacy);
  ResponseSimulator valid(config);
  (void)valid;

  assert(setenv("CCB_SIPM_PROMPT_CROSSTALK_PARENT_RECOVERY_MODEL",
                "TYPO_MUST_FAIL_CLOSED", 1) == 0);
  ModelConfig invalid = ModelConfig::RepresentativeS13360_3050CS();
  (void)ModelConfig::ApplyEnvironmentOverrides(invalid);
  bool threw = false;
  try {
    ResponseSimulator rejected(invalid);
    (void)rejected;
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert(threw);

  unsetenv("CCB_SIPM_PROMPT_CROSSTALK_PARENT_RECOVERY_MODEL");
  unsetenv("CCB_SIPM_DELAYED_CROSSTALK_PARENT_RECOVERY_MODEL");
  unsetenv("CCB_SIPM_AFTERPULSE_PARENT_RECOVERY_MODEL");
  return 0;
}
