# Impulse digest canonicalization v1

This document defines the bounded digest primitive used by `ARU-ELEC-IMPULSE-DIGEST-CANONICALIZATION-001` under `ccb-testbeam#1067`.

## Scientific and provenance boundary

Three objects must remain distinct:

1. **external source bytes** — the calibration file/object exactly as acquired or archived;
2. **parsed numerical sample payload** — the in-memory `(time_ns, amplitude)` pairs supplied to the SiPM core;
3. **effective runtime kernel** — the resampled, normalized kernel actually evaluated by waveform synthesis on a declared sample spacing and support length.

A digest of (2) is not a digest of (1). A digest of (3) is not evidence that either (1) or (2) came from a measured/calibrated device. These identities are necessary provenance components but do not by themselves authorize `MEASURED` status.

SHA-256 follows NIST FIPS 180-4 (`doi:10.6028/NIST.FIPS.180-4`).

## Canonical sample-payload bytes

`CCB_SIPM_IMPULSE_SAMPLES_V1\0 || count_u64_le || repeated(time_f64_le || amplitude_f64_le)`

where each floating-point value must be finite IEC-559/IEEE-754 binary64. Zero is canonicalized numerically so `-0.0` and `+0.0` map to the same bytes. `count` is the exact number of sample pairs.

The returned string is `sha256:<64 lowercase hex digits>`.

This contract intentionally does **not** include a source label, URL, retrieval date, or caller-provided source hash. Those are provenance metadata, not the numerical payload identity.

## Canonical effective-kernel bytes

`CCB_SIPM_EFFECTIVE_KERNEL_V1\0 || sample_dt_ns_f64_le || count_u64_le || repeated(kernel_value_f64_le)`

The sample spacing and exact kernel length are part of the identity. Therefore a history extension that appends additional elapsed-time support changes the digest even if the original source samples are unchanged. As above, all values must be finite binary64 and signed zero is collapsed.

The kernel supplied to this digest must be the actual effective kernel after the production resampling/normalization step. A later integration child must bind this digest directly to the same kernel object used by waveform synthesis rather than reconstructing it independently.

## Identifiability and surviving children

The following implications are invalid and remain rejected:

- `sample_payload_hash present => external source bytes identified`;
- `effective_kernel_hash present => calibration is measured/validated`;
- `caller supplied source-hash-shaped string => source digest verified`.

Surviving children are:

- bind exact external source bytes and their SHA-256 to the parser producing the sample payload;
- bind `CanonicalEffectiveKernelHash` to the exact history-complete runtime kernel object used by `ResponseSimulator`;
- validate units, polarity, baseline, time zero, normalization and resampling observables;
- only then define a positive `CUSTOM_UNVALIDATED -> MEASURED` authorization transition;
- audit historical outputs that serialized `MEASURED` without these gates.

No detector calibration, beam data, production MC, or detector-performance claim is established by this digest primitive.
