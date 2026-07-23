# Contributing

1. Read `CLEAN_ROOM_POLICY.md`.
2. Open an issue describing the claim, model or defect.
3. Add tests and provenance before implementation.
4. Use a focused branch and pull request.
5. Run `bash tools/run_quality_gate.sh`.
6. Label generated evidence as synthetic, software-verification, device-
   validation or detector-validation.
7. Update the claim-evidence matrix and relevant model card.

Contributions must not introduce undocumented physical constants or silent
fallbacks. Parameter ranges and units are part of the API.
