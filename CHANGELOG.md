# Changelog

## 0.2.0-dev — 2026-07-24

- SIPM-P0-004 / SIPM-P1-003 (model + provenance): the
  `RepresentativeS13360_3050CS` device profile now carries a structured
  `DeviceProfileProvenance` (source ids SRC-HAMA-001 / SRC-HAMA-002, source
  URLs, retrieved date 2026-07-23, overvoltage, temperature, covered
  parameters) and emits it into run metadata via `RunMetadata::render_json()`.
  Calibration status stays `MANUFACTURER_REPRESENTATIVE_NOT_CALIBRATED`;
  device-specific validation (V-DEV-*, V-ELEC-*) is an explicit open item.
  DCR / recovery / crosstalk / afterpulse defaults are recorded with the same
  provenance. The device-profile JSON carries the matching structured
  provenance block.
- SIPM-P1-004 (generic electronics): the front-end is a configurable
  CR-RC(-RC) semi-gaussian shaper (`shaper_integrator_stages`, default 1)
  convolved with the microcell delta-train, followed by ADC sampling /
  quantisation. Readout window, shaper tau/stages and ADC bits are
  CLI-overridable in `ccb_sipm_toy` and env-overridable via
  `ModelConfig::ApplyEnvironmentOverrides()` (`CCB_SIPM_*` keys). A measured
  single-PE impulse can replace the generic kernel
  (`ModelConfig::measured_impulse_*`), flipping the electronics provenance to
  `MEASURED`. Until supplied, the status stays
  `ASSUMPTION_GENERIC_CRRC_NOT_MEASURED`.
- Tests extended: provenance fields present, PDE curve matches the source,
  metadata JSON carries all provenance keys, shaper impulse causal/finite/
  peak-normalised, stages=2 broader than stages=1, ADC range respected,
  measured-impulse hook exercised, environment overrides applied.

## 0.1.0-dev — 2026-07-23

- initial clean-room public-project scaffold;
- reference C++17 implementation and Geant4 adapter boundary;
- research, analysis, V&V/UQ and provenance policies;
- synthetic demonstration and diagnostic pipeline.
