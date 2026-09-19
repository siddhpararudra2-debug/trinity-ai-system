"""
Trinity Core Configuration
--------------------------
Single source of truth for paths and runtime settings.
V1 intentionally avoids env-driven complexity: everything resolves
relative to the repo root so the system runs with zero setup.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
STORAGE_ROOT = Path(os.getenv("TRINITY_STORAGE_ROOT", str(REPO_ROOT / "data")))
DB_PATH = Path(os.getenv("TRINITY_DB_PATH", str(STORAGE_ROOT / "trinity.db")))


@dataclass(frozen=True)
class Settings:
    repo_root: Path = REPO_ROOT
    storage_root: Path = STORAGE_ROOT

    db_path: Path = DB_PATH

    projects_dir: Path = STORAGE_ROOT / "projects"
    artifacts_dir: Path = STORAGE_ROOT / "artifacts"
    jobs_dir: Path = STORAGE_ROOT / "jobs"
    cad_dir: Path = STORAGE_ROOT / "cad"
    exports_dir: Path = STORAGE_ROOT / "exports"
    temp_dir: Path = STORAGE_ROOT / "temp"
    cache_dir: Path = STORAGE_ROOT / "cache"
    embeddings_dir: Path = STORAGE_ROOT / "embeddings"
    evaluation_dir: Path = STORAGE_ROOT / "evaluation"

    api_title: str = "Trinity AI — Engineering Operating System"
    api_version: str = "1.0.0"

    def all_storage_dirs(self) -> list[Path]:
        return [
            self.projects_dir,
            self.artifacts_dir,
            self.jobs_dir,
            self.cad_dir,
            self.exports_dir,
            self.temp_dir,
            self.cache_dir,
            self.embeddings_dir,
            self.evaluation_dir,
        ]


settings = Settings()


def ensure_storage_layout() -> None:
    """Create the on-disk storage tree if it doesn't exist yet. Idempotent."""
    for d in settings.all_storage_dirs():
        d.mkdir(parents=True, exist_ok=True)
