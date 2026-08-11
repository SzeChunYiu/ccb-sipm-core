# Impulse runtime-kernel binding v1

This document defines the bounded runtime-object provenance contract for `ARU-ELEC-IMPULSE-DIGEST-RUNTIME-BINDING-001`, a child of `ccb-testbeam#1067` and successor to the canonical digest primitive in `impulse_digest_v1.md`.

## Input/output contract

Inputs are the validated `ModelConfig` waveform interval, history interval, sample spacing, and impulse-model parameters or sampled impulse values. All time quantities are nanoseconds. The observable is the exact peak-normalised numerical impulse kernel consumed by waveform convolution, not an independently reconstructed or caller-described kernel.

For

- `N_out = floor((window_end_ns - window_start_ns) / sample_dt_ns) + 1`,
- `N_hist = ceil(max(0, window_start_ns - history_start_ns) / sample_dt_ns)`,
- `N_kernel = N_out + N_hist`,

a waveform-producing `ResponseSimulator` constructs one private `waveform_kernel_` object with exactly `N_kernel` samples. `make_waveform()` reads that object by const reference. `run_metadata()` hashes that same object by const reference using

`CanonicalEffectiveKernelHash(sample_dt_ns, waveform_kernel_)`.

The resulting `electronics.effective_kernel_hash` is therefore an identity of the effective numerical kernel actually available to convolution for that simulator instance.

If `generate_waveform=false`, no runtime waveform kernel is consumed and the effective-kernel hash is deliberately empty.

## Waveform semantics

For each accepted avalanche `a` with amplitude `A_a` and time `t_a`, each recorded sample at `t_i >= t_a` receives

`V_i += A_a h(t_i - t_a)`

where `h` is evaluated from the cached history-complete kernel with the existing continuous fractional-delay interpolation. The runtime-binding change does not alter that numerical interpolation law; it changes object lifetime/provenance so the hashed kernel and consumed kernel cannot drift as separate reconstructions.

## Equivalence and identifiability

A positive global scale applied to a sampled source impulse is observationally equivalent at this stage when peak normalisation maps it to the same effective kernel. Such source representations intentionally have the same `effective_kernel_hash` even though their source/sample-payload hashes differ.

Shape changes, sample spacing changes, and history-support length changes alter the effective kernel identity. This is exactly why source identity, parsed-sample identity, and effective-kernel identity remain separate provenance objects.

A caller-provided `electronics.effective_kernel_hash` is never trusted. When waveform generation is active it is overwritten with the digest of the cached consumed kernel. When waveform generation is disabled it is cleared.

## Scientific boundary

This binding does not establish that a sampled impulse is a measured/calibrated electronics response. `CUSTOM_UNVALIDATED` remains the fail-closed state for sampled kernels until exact source bytes, parser/sample binding, physical calibration observables, resampling validation, and an explicit positive authorization transition are all established.

It also does not determine the physically sufficient pre-window history horizon. The kernel length is complete with respect to the *declared* history interval; convergence of that declaration is a separate atom under `ccb-testbeam#1096`.

## Required regression controls

`tests/test_impulse_runtime_binding.cc` freezes the following discriminators:

- known canonical digest and exact unit-response waveform for a three-sample `{0,1,0}` effective kernel;
- known canonical digest for the four-sample history extension `{0,1,0,0}` plus a history-boundary avalanche consuming ages 1, 2, and 3 ns;
- caller-forged effective-kernel hash overwrite;
- positive global source-scale equivalence after peak normalization;
- shape-change digest sensitivity;
- fail-closed empty effective-kernel identity when no waveform is generated.

These are software/provenance tests. They are not beam data, detector calibration, or production Monte Carlo validation.
