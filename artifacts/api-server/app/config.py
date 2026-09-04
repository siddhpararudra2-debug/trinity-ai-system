from pydantic_settings import BaseSettings
from functools import lru_cache


class Settings(BaseSettings):
    """Application settings loaded from environment."""
    database_url: str = "sqlite+aiosqlite:///./data/trinity.db"
    openai_api_key: str | None = None
    anthropic_api_key: str | None = None
    google_api_key: str | None = None
    artifact_dir: str = "./artifacts"
    disable_docs: bool = False
    auth_secret: str = ""
    api_key: str = ""
    auth_required: bool = False
    cors_origins: str = "*"
    bootstrap_admin_email: str = ""
    bootstrap_admin_password: str = ""
    redis_url: str = ""
    rate_limit_per_minute: int = 60

    model_config = {
        "env_prefix": "TRINITY_",
        "env_file": ".env",
        "env_file_encoding": "utf-8",
        "extra": "ignore",
    }


@lru_cache
def get_settings() -> Settings:
    """Return cached settings instance."""
    return Settings()
