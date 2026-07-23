# Clean-room implementation policy

## Permitted inputs

Contributors may use:

- peer-reviewed papers, preprints and textbooks for physics;
- manufacturer documentation and independently measured data;
- official public APIs and file formats;
- black-box behavior of external software under its public interface;
- independently written requirements and test vectors.

## Prohibited inputs

Do not copy or paraphrase source code, comments, internal class structure,
private tests, examples, documentation passages, parameter files or artwork
from G4SiPM or another project unless a reviewed compatible dependency is being
added with full licence compliance.

A contributor who has inspected external source may still contribute, but every
substantial implementation must be traceable to an independently written model
specification and must not preserve distinctive expression from that source.

## Required record

Every physics implementation PR must contain:

1. model identifier and equations;
2. primary literature;
3. independent pseudocode written before implementation;
4. assumptions and applicability domain;
5. test or validation source;
6. explicit statement of external source code not copied;
7. reviewer confirmation.

The clean-room ledger is append-only:
`references/CLEAN_ROOM_LEDGER.csv`.
