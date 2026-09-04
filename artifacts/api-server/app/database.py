"""SQLite async database layer (aiosqlite + SQLAlchemy)."""
import os
from sqlalchemy.ext.asyncio import (
    create_async_engine,
    AsyncSession,
    async_sessionmaker,
)
from sqlalchemy.orm import DeclarativeBase
from sqlalchemy import inspect, select, text

from app.config import get_settings

def _get_engine():
    db_url = get_settings().database_url
    if db_url and "sqlite" in db_url and ":///" in db_url:
        db_file_path = db_url.split(":///", 1)[-1]
        if db_file_path and db_file_path != ":memory:":
            db_dir = os.path.dirname(os.path.abspath(db_file_path))
            if db_dir:
                os.makedirs(db_dir, exist_ok=True)
    return create_async_engine(db_url, echo=False)


engine = _get_engine()
AsyncSessionLocal = async_sessionmaker(
    engine, class_=AsyncSession, expire_on_commit=False
)


class Base(DeclarativeBase):
    pass


async def init_db() -> None:
    """Create tables and apply the additive conversation ownership migration."""
    async with engine.begin() as conn:
        from app import models  # noqa: F401 — registers ORM models
        await conn.run_sync(Base.metadata.create_all)

        def migrate(sync_conn):
            inspector = inspect(sync_conn)
            if "conversations" not in inspector.get_table_names():
                return
            columns = {column["name"] for column in inspector.get_columns("conversations")}
            if "owner_id" not in columns:
                sync_conn.execute(text("ALTER TABLE conversations ADD COLUMN owner_id INTEGER"))
                sync_conn.execute(text("CREATE INDEX IF NOT EXISTS ix_conversations_owner_id ON conversations (owner_id)"))

        await conn.run_sync(migrate)

    admin_email = os.getenv("TRINITY_BOOTSTRAP_ADMIN_EMAIL", "").strip().lower()
    admin_password = os.getenv("TRINITY_BOOTSTRAP_ADMIN_PASSWORD", "")
    if admin_email and admin_password:
        from app.auth import hash_password
        from app.models import User
        async with AsyncSessionLocal() as session:
            result = await session.execute(select(User).where(User.email == admin_email))
            user = result.scalar_one_or_none()
            if user is None:
                session.add(User(email=admin_email, password_hash=hash_password(admin_password), role="admin"))
                await session.commit()
            elif user.role != "admin":
                user.role = "admin"
                await session.commit()


async def get_db():
    """FastAPI dependency — yields an async DB session."""
    async with AsyncSessionLocal() as session:
        yield session
