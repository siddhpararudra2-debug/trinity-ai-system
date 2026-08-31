from pydantic_settings import BaseSettings
from functools import lru_cache


class Settings(BaseSettings):
    """Application settings loaded from environment."""
    # Use TRINITY_ prefix so DATABASE_URL (PostgreSQL) is not picked up
    database_url: str = "sqlite+aiosqlite:///./trinity.db"
    openai_api_key: str | None = None
    anthropic_api_key: str | None = None
    google_api_key: str | None = None

    model_config = {
        "env_prefix": "TRINITY_",
        "env_file": ".env",
        "env_file_encoding": "utf-8",
        "extra": "ignore",
    }


@lru_cache()
def get_settings() -> Settings:
    """Return cached settings instance."""
    return Settings()
