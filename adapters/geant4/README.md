# Optional Geant4 adapter

This adapter only converts an optical boundary crossing into a
`PhotonArrival`. It deliberately does not:

- apply PDE;
- generate a Geiger avalanche;
- own event state;
- kill the track;
- depend on CCB `EventAction`.

The CCB integration should keep sensor physical-volume pointers, call
`FromStep`, append accepted arrivals to event data, and then apply the chosen
surface/absorption contract. Add boundary-status and creator metadata in the
repository integration; this minimal adapter demonstrates the dependency
boundary.
