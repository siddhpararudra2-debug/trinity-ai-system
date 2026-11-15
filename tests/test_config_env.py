"""The .env.dummy contract must hold for every importer of config.

src/core/config.py reads os.getenv at import time, so the env file has
to be loaded by config itself. main.py used to own that load, and any
module importing config before main — src.api.accounts does exactly
that — bound empty strings permanently: the M31 trust copy came up
blank in the full suite while passing in isolation (pytest collects
alphabetically, so test_accounts.py always imported accounts first).

Each case runs in a clean subprocess: no src.main import, no dotenv
side effects left behind by earlier tests.
"""

from __future__ import annotations

import subprocess
import sys

import pytest

from src.core.config import REPO_ROOT

_SNIPPET = (
    "import src.api.accounts as a\n"
    "import src.api.trust as t\n"
    "assert a.JWT_SECRET, 'accounts: JWT secret blank without src.main'\n"
    "assert t.TRINITY_REFUND_CANCELLATION, "
    "'trust: refund copy blank without src.main'\n"
)


@pytest.mark.parametrize(
    "case",
    [
        pytest.param(
            ("accounts imports config before main", _SNIPPET),
            id="accounts-before-main",
        ),
    ],
)
def test_config_sees_the_env_file_without_main(case: tuple[str, str]):
    name, snippet = case
    result = subprocess.run(
        [sys.executable, "-c", snippet],
        cwd=str(REPO_ROOT),
        capture_output=True,
        text=True,
        timeout=120,
    )
    assert result.returncode == 0, f"{name} failed:\n{result.stderr}"
