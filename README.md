# ccb-sipm-core prototype

A clean-room C++17 prototype for the CCB stave SiPM response.

## Implemented

- wavelength-dependent PDE interpolation;
- deterministic event/sensor RNG;
- local-position microcell mapping;
- finite cells and exponential recovery;
- dark counts;
- prompt and delayed crosstalk;
- two afterpulse components;
- gain variation and SPTR;
- causal pulse convolution, analog noise and ADC;
- configurable CR-RC(-RC) shaper with CLI/env overrides and a measured-impulse hook;
- device-profile provenance (SRC-HAMA-001/002) and run-metadata emission;
- optional minimal Geant4 arrival adapter;
- unit tests and synthetic campaign.

## Deliberate limitations

- representative, not calibrated parameters;
- no bias/temperature surface interpolation yet;
- simplified crosstalk/recovery trigger law;
- no measured electronics impulse response (generic CR-RC(-RC) default; measured hook implemented);
- no Geant4 production integration;
- no radiation-damage model.

The prototype demonstrates interfaces and tests. Physics acceptance requires the
validation matrix in the handoff package.
