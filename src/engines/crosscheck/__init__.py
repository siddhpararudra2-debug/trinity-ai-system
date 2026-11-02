"""Cross-check layer (Stage 11): the design record and its checks.

``record.py`` (M20.1) carries the shared design record — parts, nets and
pins, board size, holes and connectors, enclosure, firmware pin map —
assembled from the engines' existing JSON answers, persisted per project
under ``TRINITY_STORAGE_ROOT/projects/<id>/record.json``.

No registry op is advertised yet (catalog honesty): the checker, report,
and routes land in M20.2+ one concern per commit. Nothing in this package
consults a model — verification is deterministic by decision
(`decided.md`, Phase 0).
"""
