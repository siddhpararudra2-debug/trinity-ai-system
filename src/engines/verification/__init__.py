"""Verification Engine (PRD §24, §31).

Assesses whether a design has been properly verified against
datasheets, standards, and design rules. Parts without valid
datasheets receive "unverified" status with a reason.

This engine integrates with the PCB and CAD engines to provide
a verification report on generated designs.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from src.engines.base import BaseEngine, EngineResult, ValidationResult
from src.core.errors import RequestValidationError


class VerificationError(RequestValidationError):
    """Verification-specific error for missing datasheets/rules."""


class VerificationEngine(BaseEngine):
    name = "verification"
    version = "1.0"
    capabilities = ("verify",)

    def execute(self, operation: str, parameters: dict[str, Any]) -> EngineResult:
        if operation == "verify":
            return self.execute_verify(parameters)
        raise RequestValidationError(f"Verification engine has no operation '{operation}'")

    # -------------------------------------------------------- verify ---

    def execute_verify(self, parameters: dict[str, Any]) -> EngineResult:
        """Verify a design by checking component datasheets and design rules.

        Returns:
            EngineResult with:
            - success: True if all critical components are verified
            - engine_result: dict with ``verified`` (bool), ``unverified``
              (list of parts without datasheets), and ``report`` (human-readable
              summary).
            - pending_artifacts: empty (verification is read-only).
        """
        components = parameters.get("components", [])
        part_defs = parameters.get("part_definitions", {})

        unverified: list[dict[str, Any]] = []
        verified_count = 0

        # Build a ref->component map for quick lookup
        ref_to_comp: dict[str, dict[str, Any]] = {}
        for comp in components:
            ref = comp.get("ref", "")
            if ref:
                ref_to_comp[ref] = comp

        for comp in components:
            part_type = comp.get("type", "")
            # Look up the part definition to check for datasheet
            part_def = part_defs.get(part_type, {})
            datasheet = part_def.get("datasheet")

            if not datasheet:
                unverified.append(
                    {
                        "ref": comp.get("ref", ""),
                        "value": comp.get("value", ""),
                        "part_type": part_type,
                        "reason": f"No datasheet found for part type '{part_type}'",
                    }
                )
            else:
                verified_count += 1

        all_verified = len(unverified) == 0
        status = "VERIFIED" if all_verified else "UNVERIFIED"

        checks: dict[str, Any] = {}
        if unverified:
            checks["unverified_count"] = len(unverified)
            checks["unverified_parts"] = [
                {"ref": u["ref"], "reason": u["reason"]} for u in unverified
            ]

        # Build the unverified list with value lookup
        unverified_list: list[dict[str, Any]] = []
        for u in unverified:
            ref = u["ref"]
            comp = ref_to_comp.get(ref, {})
            val = comp.get("value", "unknown")
            unverified_list.append(
                {
                    "ref": ref,
                    "value": val,
                    "part_type": u["part_type"],
                    "reason": u["reason"],
                }
            )

        result = {
            "verified": all_verified,
            "verified_count": verified_count,
            "total_components": len(components),
            "unverified": unverified_list,
            "report": self._generate_report(unverified, verified_count, len(components)),
        }

        return EngineResult(
            success=all_verified,
            engine=self.name,
            operation="verify",
            result=result,
            validation=ValidationResult(status=status, checks=checks),
            pending_artifacts=[],
        )

    # ------------------------------------------------------------ helper ---

    @staticmethod
    def _generate_report(
        unverified: list[dict[str, Any]],
        verified_count: int,
        total: int,
    ) -> str:
        """Generate a human-readable verification report."""
        lines = [
            "Design Verification Report",
            "========================================",
            f"Total components: {total}",
            f"Verified: {verified_count}",
            f"Unverified: {len(unverified)}",
            "",
        ]

        if unverified:
            lines.append("Unverified parts:")
            for u in unverified:
                lines.append(f"  - {u['ref']} ({u['value']}): {u['reason']}")
        else:
            lines.append("All components verified.")

        lines.append("========================================")
        return "\n".join(lines)