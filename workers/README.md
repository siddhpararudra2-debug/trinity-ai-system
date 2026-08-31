# Trinity workers

Workers are explicit integrations for actions that the API should not execute in its own process.

- [`firmware/`](firmware/README.md) — container definition for a trusted firmware build environment.
- [`fusion/`](fusion/README.md) — Fusion 360 desktop add-in for signed STEP/STL/F3D export.
- [`kicad/`](kicad/README.md) — fixed-command KiCad 9 ERC/DRC and manufacturing worker.

Workers must receive secrets through a secret manager or environment, use job-scoped directories, preserve logs and hashes, and require human engineering review for manufacturing or flight-facing outputs.
