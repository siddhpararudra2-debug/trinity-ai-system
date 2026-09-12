# Trinity architecture

`API → structured request → job manager → engine registry → engine → validation → artifact manager → SQLite lineage`.

The built-in CAD engine uses a library-independent IR, deterministic native mesh builder, independent validator, and binary STL/GLB exporters. A future model provider can emit structured tool calls but never executes engines itself.
