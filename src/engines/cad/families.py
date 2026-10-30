"""Parameter families (M14) - one part model, many verified variants.

A family takes one registered part, one catalog parameter, and a list
of values. Every member is a FULL specification: its own parameter set,
validation outcome, geometry stats, and content checksum. Members whose
values violate the part's own request rules are reported `valid: false`
with the real reason - never dropped, never blamed on the whole family,
because the family's job is to tell the truth about every variant.

The op is catalog-driven by construction: the family parameter must
exist in the part's catalog, and the per-member checks are exactly the
checks single-part generation enforces.
"""

from __future__ import annotations

import hashlib
import json
import math
from typing import Any

from src.core.errors import RequestValidationError
from src.engines.cad.parts import PartDefinition
from src.engines.cad.primitives import Mesh

# Upper bound on one family call: each member builds real geometry, so
# a runaway value list would be a runaway job.
MAX_FAMILY_MEMBERS = 24


def _fmt(value: float | int) -> str:
    """Variant label number: whole floats render as ints (2.0 -> 2)."""
    if isinstance(value, float) and value.is_integer():
        return str(int(value))
    return repr(float(value))


def _canonical_checksum(payload: Any) -> str:
    """Stable content fingerprint: sorted keys, trimmed float noise."""
    return hashlib.sha256(
        json.dumps(
            payload,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=True,
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()


def _mesh_checksum(mesh: Mesh, spec: dict[str, Any]) -> str:
    """Content checksum over the exact triangle soup plus the spec."""
    return _canonical_checksum(
        {
            "spec": spec,
            "triangle_count": len(mesh.triangles),
            "triangles": [
                [list(v) for v in triangle] for triangle in mesh.triangles
            ],
        }
    )


def validate_family_selector(
    part: PartDefinition, family_param: str, values: list[Any]
) -> list[float]:
    """Shared input gate for the families op; raises with catalog truth."""
    known = [spec.name for spec in part.parameters]
    if family_param not in known:
        raise RequestValidationError(
            f"Unknown CAD family parameter '{family_param}'",
            details={"supported_parameters": known},
        )
    if not isinstance(values, list) or not values:
        raise RequestValidationError("family values must be a non-empty list")
    if len(values) > MAX_FAMILY_MEMBERS:
        raise RequestValidationError(
            f"family values must have at most {MAX_FAMILY_MEMBERS} entries"
        )
    cleaned: list[float] = []
    for value in values:
        # bool is an int subclass; a boolean thickness is never real input.
        if (
            isinstance(value, bool)
            or not isinstance(value, (int, float))
            or not math.isfinite(value)
        ):
            raise RequestValidationError(
                "family values must be finite numbers"
            )
        cleaned.append(float(value))
    return cleaned


def build_family(
    part: PartDefinition,
    base_params: dict[str, Any],
    family_param: str,
    values: list[Any],
) -> dict[str, Any]:
    """Build every member; report truth per member.

    The base parameter set is never assumed valid on its own: any
    member error surfaces on THAT member, so a caller sees exactly
    which variants are usable and why.
    """
    cleaned = validate_family_selector(part, family_param, values)
    members: list[dict[str, Any]] = []
    for index, value in enumerate(cleaned):
        member_params: dict[str, Any] = {**base_params, family_param: value}
        member: dict[str, Any] = {
            "index": index,
            "variant": f"{family_param}={_fmt(value)}",
            "parameters": member_params,
        }
        try:
            ir = part.ir_class.from_request(member_params)
            mesh = part.builder(ir)
            ok, checks = part.validator(ir, mesh)
            spec = ir.to_dict()
            member.update(
                valid=ok,
                reason=None if ok else "geometry validation failed",
                checks=checks,
                spec=spec,
                triangle_count=len(mesh.triangles),
                bounding_box_mm=mesh.bounding_box(),
                checksum=_mesh_checksum(mesh, spec),
            )
        except RequestValidationError as exc:
            member.update(
                valid=False,
                reason=str(exc),
                checks=None,
                spec=None,
                triangle_count=None,
                bounding_box_mm=None,
                checksum=None,
            )
        members.append(member)
    valid_count = sum(1 for m in members if m["valid"])
    return {
        "type": part.name,
        "family_parameter": family_param,
        "count": len(members),
        "valid_count": valid_count,
        "members": members,
    }
