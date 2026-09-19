"""Managing API endpoint limits (simple in-memory token bucket)."""
from __future__ import annotations

import time


class RateLimiter:
    def __init__(self, calls_per_minute: int = 60):
        self.interval = 60.0 / calls_per_minute
        self._last = 0.0

    def acquire(self) -> None:
        now = time.monotonic()
        wait = self._last + self.interval - now
        if wait > 0:
            time.sleep(wait)
        self._last = time.monotonic()
