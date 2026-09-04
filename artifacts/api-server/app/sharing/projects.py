"""Forkable project links — share and fork artifact stacks."""
from __future__ import annotations

import json
import secrets
import uuid
from datetime import datetime, timezone
from typing import Any

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models import SharedProject


class ProjectSharingService:
    async def create_share(
        self,
        db: AsyncSession,
        owner_id: int,
        title: str,
        manifest: dict[str, Any],
        public: bool = True,
    ) -> SharedProject:
        token = secrets.token_urlsafe(16)
        project = SharedProject(
            id=f"proj_{uuid.uuid4().hex}",
            owner_id=owner_id,
            share_token=token,
            title=title[:255],
            manifest_json=json.dumps(manifest, sort_keys=True),
            public=public,
            forked_from_id=None,
        )
        db.add(project)
        await db.commit()
        await db.refresh(project)
        return project

    async def get_by_token(self, db: AsyncSession, token: str) -> SharedProject | None:
        result = await db.execute(select(SharedProject).where(SharedProject.share_token == token))
        return result.scalar_one_or_none()

    async def fork(
        self,
        db: AsyncSession,
        source: SharedProject,
        new_owner_id: int,
    ) -> SharedProject:
        manifest = json.loads(source.manifest_json)
        manifest["provenance"] = {
            "forked_from": source.id,
            "forked_at": datetime.now(timezone.utc).isoformat(),
            "source_title": source.title,
        }
        fork = SharedProject(
            id=f"proj_{uuid.uuid4().hex}",
            owner_id=new_owner_id,
            share_token=secrets.token_urlsafe(16),
            title=f"Fork of {source.title}"[:255],
            manifest_json=json.dumps(manifest, sort_keys=True),
            public=source.public,
            forked_from_id=source.id,
        )
        db.add(fork)
        await db.commit()
        await db.refresh(fork)
        return fork

    @staticmethod
    def serialize(project: SharedProject, base_url: str = "") -> dict[str, Any]:
        prefix = base_url.rstrip("/")
        return {
            "project_id": project.id,
            "title": project.title,
            "share_url": f"{prefix}/p/{project.share_token}" if prefix else f"/p/{project.share_token}",
            "share_token": project.share_token,
            "public": project.public,
            "forked_from_id": project.forked_from_id,
            "manifest": json.loads(project.manifest_json),
            "created_at": project.created_at.isoformat() if project.created_at else None,
        }
