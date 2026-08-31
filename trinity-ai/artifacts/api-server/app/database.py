"""SQLite async database layer (aiosqlite + SQLAlchemy)."""
from sqlalchemy.ext.asyncio import (
    create_async_engine,
    AsyncSession,
    async_sessionmaker,
)
from sqlalchemy.orm import DeclarativeBase

from app.config import get_settings

settings = get_settings()

engine = create_async_engine(settings.database_url, echo=False)
AsyncSessionLocal = async_sessionmaker(
    engine, class_=AsyncSession, expire_on_commit=False
)


class Base(DeclarativeBase):
    pass


async def init_db() -> None:
    """Create all tables if they don't exist."""
    async with engine.begin() as conn:
        from app import models  # noqa: F401 — registers ORM models
        await conn.run_sync(Base.metadata.create_all)


async def get_db():
    """FastAPI dependency — yields an async DB session."""
    async with AsyncSessionLocal() as session:
        yield session
