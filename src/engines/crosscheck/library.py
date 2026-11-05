"""The reference library and candidate queue (M24).

- ``data/reference/library/<id>/record.json`` holds verified designs,
  tagged with ``tags[]`` and meta at ``data/reference/library/<id>/meta.json``
  (`{design_id, tags[], added_on, verified, notes}`).

- ``data/reference/candidates/<id>/record.json`` holds machine-suggested
  fixes with meta adding ``probes``, ``proposed_on``, ``queue``.  Only
  ``promote_candidate(id)`` moves a candidate into the library, and it
  re-runs the verified report in the same call and refuses unless the
  candidate comes back ``VERIFIED`` — no auto-promotion, ever.

- Retrieval is a tag-containment rank over the verified ones:
  ``matching(design_id, target_tags)`` returns a score of 1.0 for exact-set
  equality and 0.7 for single-tag carry; ``matching_names()`` returns the
  top‑n matching design ids.
"""

from __future__ import annotations

import json
import datetime
import shutil
from pathlib import Path
from typing import Any

from src.engines.crosscheck.record import DesignRecord, DesignRecordError
from src.engines.crosscheck.report import assemble_report

REPO_ROOT = Path(__file__).resolve().parents[2]
LIBRARY_ROOT = REPO_ROOT / "data" / "reference" / "library"
CANDIDATES_ROOT = REPO_ROOT / "data" / "reference" / "candidates"

__all__ = [
    "add_candidate",
    "candidate_designs",
    "library_designs",
    "matching",
    "matching_names",
    "promote_candidate",
    "qualify",
]


def _library_path(design_id: str) -> Path:
    """Path under ``data/reference/library/`` for a design."""
    return LIBRARY_ROOT / design_id


def _candidate_path(design_id: str) -> Path:
    """Path under ``data/reference/candidates/`` for a design."""
    return CANDIDATES_ROOT / design_id


def _meta_path(design_id: str) -> Path:
    """Meta file path (library or candidate)."""
    return _library_path(design_id) / "meta.json"


def _record_path(design_id: str) -> Path:
    """Record file path (library or candidate)."""
    return _library_path(design_id) / "record.json"


def _candidate_meta_path(design_id: str) -> Path:
    """Candidate meta file path."""
    return _candidate_path(design_id) / "meta.json"


def _ensure_dirs() -> None:
    """Create library and candidates directories if they don't exist."""
    LIBRARY_ROOT.mkdir(parents=True, exist_ok=True)
    CANDIDATES_ROOT.mkdir(parents=True, exist_ok=True)


def _load_design_record(design_id: str) -> DesignRecord:
    """Load a DesignRecord from the library or candidates for the given id.

    Reads ``data/reference/library/<id>/record.json`` or
    ``data/reference/candidates/<id>/record.json`` and parses it via
    ``DesignRecord.from_dict``.
    """
    for root in (LIBRARY_ROOT, CANDIDATES_ROOT):
        path = root / design_id / "record.json"
        if path.exists():
            try:
                raw = json.loads(path.read_text(encoding="utf-8"))
                return DesignRecord.from_dict(raw)
            except (json.JSONDecodeError, DesignRecordError):
                continue
    raise DesignRecordError(f"design '{design_id}' not found in library or candidates")


def _record_to_json(record: DesignRecord) -> str:
    """Serialize a DesignRecord to JSON text via its ``to_dict()`` method."""
    return json.dumps(record.to_dict(), indent=2)


def _record_from_json(text: str) -> DesignRecord:
    """Deserialize JSON text back into a DesignRecord."""
    return DesignRecord.from_dict(json.loads(text))


def library_designs() -> list[dict[str, Any]]:
    """Return every design currently in the verified library.

    Returns a list of design summaries with overall_status and meta tags.
    """
    _ensure_dirs()
    designs: list[dict[str, Any]] = []
    for p in LIBRARY_ROOT.iterdir():
        if p.is_dir():
            design_id = p.name
            record_path = p / "record.json"
            meta_path = p / "meta.json"
            if not record_path.exists():
                continue
            try:
                record = DesignRecord.from_dict(json.loads(record_path.read_text(encoding="utf-8")))
                meta = json.loads(meta_path.read_text(encoding="utf-8")) if meta_path.exists() else {}
                # Determine overall status from engine checks
                checks = record.engine_checks
                overall = "PASS"
                if checks:
                    all_pass = all(c.status == "pass" for c in checks)
                    failed_any = any(c.status == "fail" for c in checks)
                    if all_pass:
                        overall = "VERIFIED"
                    elif failed_any:
                        overall = "FAIL"
                    else:
                        overall = "PARTIAL"
                designs.append(
                    {
                        "design_id": design_id,
                        "overall_status": overall,
                        "checks": [
                            {
                                "check_id": c.check_id,
                                "name": c.name,
                                "status": c.status,
                                "reason": c.reason,
                                "location": c.location,
                                "evidence": c.evidence,
                            }
                            for c in checks
                        ]
                        if checks
                        else [],
                        "meta": meta,
                    }
                )
            except (DesignRecordError, json.JSONDecodeError, KeyError):
                # If we can't parse the record, still add it with unknown status
                designs.append(
                    {
                        "design_id": design_id,
                        "overall_status": "UNKNOWN",
                        "checks": [],
                        "meta": json.loads(meta_path.read_text(encoding="utf-8")) if meta_path.exists() else {},
                    }
                )
    return designs


def candidate_designs() -> list[dict[str, Any]]:
    """Return every design currently in the candidate queue.

    Returns a list of design summaries with overall_status and meta tags.
    """
    _ensure_dirs()
    candidates: list[dict[str, Any]] = []
    for p in CANDIDATES_ROOT.iterdir():
        if p.is_dir():
            design_id = p.name
            record_path = p / "record.json"
            meta_path = p / "meta.json"
            if not record_path.exists():
                continue
            try:
                record = DesignRecord.from_dict(json.loads(record_path.read_text(encoding="utf-8")))
                meta = json.loads(meta_path.read_text(encoding="utf-8")) if meta_path.exists() else {}
                checks = record.engine_checks
                overall = "PASS"
                if checks:
                    all_pass = all(c.status == "pass" for c in checks)
                    failed_any = any(c.status == "fail" for c in checks)
                    if all_pass:
                        overall = "VERIFIED"
                    elif failed_any:
                        overall = "FAIL"
                    else:
                        overall = "PARTIAL"
                candidates.append(
                    {
                        "design_id": design_id,
                        "overall_status": overall,
                        "checks": [
                            {
                                "check_id": c.check_id,
                                "name": c.name,
                                "status": c.status,
                                "reason": c.reason,
                                "location": c.location,
                                "evidence": c.evidence,
                            }
                            for c in checks
                        ]
                        if checks
                        else [],
                        "meta": meta,
                    }
                )
            except (DesignRecordError, json.JSONDecodeError, KeyError):
                candidates.append(
                    {
                        "design_id": design_id,
                        "overall_status": "UNKNOWN",
                        "checks": [],
                        "meta": json.loads(meta_path.read_text(encoding="utf-8")) if meta_path.exists() else {},
                    }
                )
    return candidates


def matching(design_id: str, target_tags: list[str]) -> float:
    """Tag-containment rank over verified designs.

    Returns ``1.0`` for exact-set equality, ``0.7`` if the design's tags
    are a superset of ``target_tags`` (single-tag carry), and ``0.0`` if
    there is no overlap.
    """
    _ensure_dirs()
    design = library_designs()
    for d in design:
        if d["design_id"] == design_id:
            design_tags = d.get("meta", {}).get("tags", [])
            if not target_tags:
                return 0.0
            if set(target_tags) == set(design_tags):
                return 1.0
            if set(target_tags).issubset(set(design_tags)) and len(target_tags) < len(design_tags):
                return 0.7
            return 0.0
    return 0.0


def matching_names(target_tags: list[str], limit: int = 5) -> list[str]:
    """Return the top‑n design ids whose tags contain all of target_tags.

    Designs are ranked by matching score (1.0 > 0.7 > 0.0) and then by
    insertion order (older designs rank higher when scores tie).
    """
    _ensure_dirs()
    design = library_designs()
    scored: list[tuple[float, int, str]] = []
    for idx, d in enumerate(design):
        tags = d.get("meta", {}).get("tags", [])
        if not target_tags:
            score = 0.0
        elif set(target_tags) == set(tags):
            score = 1.0
        elif set(target_tags).issubset(set(tags)) and len(target_tags) < len(tags):
            score = 0.7
        else:
            score = 0.0
        if score > 0.0:
            scored.append((score, idx, d["design_id"]))
    scored.sort(key=lambda x: (-x[0], x[1]))
    return [d[2] for d in scored[:limit]]


def add_candidate(design_id: str, proposed_by: str = "model", probes: list[str] | None = None) -> str:
    """Queue a design for human review.

    The candidate record is stored at
    ``data/reference/candidates/<id>/record.json`` with a meta file at
    ``data/reference/candidates/<id>/meta.json`` that records
    ``probes``, ``proposed_on``, and ``queue``.  The design is *not* promoted
    to the library automatically.
    """
    _ensure_dirs()

    # If a candidate with this id already exists, return it
    if _candidate_path(design_id).exists():
        return design_id

    # If a library design with this id already exists, cannot re-candidate
    if _library_path(design_id).exists():
        raise DesignRecordError(f"design '{design_id}' is already in the library")

    # Load the design record from the library
    library_record_path = _library_path(design_id) / "record.json"
    if not library_record_path.exists():
        raise DesignRecordError(f"design '{design_id}' not found in library or candidates")

    # Read and parse the record
    raw = json.loads(library_record_path.read_text(encoding="utf-8"))
    record = DesignRecord.from_dict(raw)

    # Save as candidate (copy the record in proper format)
    candidate_dir = _candidate_path(design_id)
    candidate_dir.mkdir(parents=True, exist_ok=True)

    # Save record in proper format
    record_path = candidate_dir / "record.json"
    with open(record_path, "w", encoding="utf-8") as f:
        f.write(_record_to_json(record))

    # Write meta
    meta = {
        "design_id": design_id,
        "proposed_by": proposed_by,
        "proposed_on": datetime.datetime.now(datetime.UTC).isoformat(),
        "queue": "pending",
        "probes": probes or [],
    }
    (candidate_dir / "meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")

    return design_id


def qualify(design_id: str) -> bool:
    """Re-run the verified report for a candidate and return whether it VERIFIED.

    This is a gate — ``promote_candidate`` refuses unless ``qualify`` returns
    ``True``.
    """
    _ensure_dirs()

    # Load the candidate record
    try:
        record = _load_design_record(design_id)
    except DesignRecordError:
        return False

    # Run the checks and assemble report
    try:
        report = assemble_report(record)
        return report.get("overall_status") == "VERIFIED"
    except DesignRecordError:
        return False


def promote_candidate(design_id: str, approved_by: str = "human") -> bool:
    """Move a candidate into the verified library.

    Refuses unless ``qualify()`` returns ``True`` (the candidate re-runs the
    verified report and comes back ``VERIFIED``).  On success the candidate
    record is deleted and the design is stored at
    ``data/reference/library/<id>/record.json`` with its meta file.
    """
    _ensure_dirs()

    if not qualify(design_id):
        return False

    # Move candidate record to library
    candidate_record_path = _candidate_path(design_id) / "record.json"
    library_record_path = _library_path(design_id) / "record.json"

    if not candidate_record_path.exists():
        return False

    # Read candidate meta to preserve notes
    candidate_meta_path = _candidate_meta_path(design_id)
    meta = {}
    if candidate_meta_path.exists():
        meta = json.loads(candidate_meta_path.read_text(encoding="utf-8"))

    # Move the record
    shutil.copy2(str(candidate_record_path), str(library_record_path))

    # Write library meta
    library_meta_path = _meta_path(design_id)
    library_meta = {
        "design_id": design_id,
        "added_on": meta.get("proposed_on"),
        "verified": True,
        "notes": meta.get("notes", ""),
        "probes": meta.get("probes", []),
    }
    library_meta_path.write_text(json.dumps(library_meta, indent=2), encoding="utf-8")

    # Delete the candidate
    candidate_meta_path.unlink(missing_ok=True)
    (candidate_record_path).unlink(missing_ok=True)
    _candidate_path(design_id).rmdir(ignore_errors=True)

    return True