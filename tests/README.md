# Tests

The Python tests are written with `unittest` and are also discoverable by pytest. They cover API route/contract presence, authentication, design artifacts and validation, firmware targets/generation/security, math parser security, durable queue races, workflow approval, vision upload ownership, and worker safety boundaries.

## Commands

```bash
uv sync --frozen --all-groups
uv run pytest -v
uv run ruff check --select E9,F .
```

The repository currently has a dependency gap: `test_api_contract.py` imports PyYAML, but PyYAML is not declared in `pyproject.toml` or `artifacts/api-server/requirements.txt`. Add the dependency before treating a clean-environment run as valid.

Tests verify application logic, not Fusion 360, KiCad, OCR binaries, target compilers, physical hardware, SITL, or flight safety.
