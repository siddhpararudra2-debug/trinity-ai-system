"""Small operational telemetry layer with no mandatory third-party services."""
from __future__ import annotations

import logging
import threading
from collections import Counter

logger = logging.getLogger("trinity.api")


class RequestMetrics:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self.requests = Counter()
        self.latency_seconds = Counter()

    def observe(self, method: str, path: str, status: int, elapsed: float) -> None:
        key = (method, path, str(status))
        with self._lock:
            self.requests[key] += 1
            self.latency_seconds[(method, path)] += elapsed

    def prometheus(self) -> str:
        lines = [
            "# HELP trinity_http_requests_total Total HTTP requests by method, path, and status.",
            "# TYPE trinity_http_requests_total counter",
        ]
        with self._lock:
            for (method, path, status), value in sorted(self.requests.items()):
                lines.append(f'trinity_http_requests_total{{method="{method}",path="{path}",status="{status}"}} {value}')
            lines.extend([
                "# HELP trinity_http_request_duration_seconds_sum Cumulative HTTP request duration.",
                "# TYPE trinity_http_request_duration_seconds_sum counter",
            ])
            for (method, path), value in sorted(self.latency_seconds.items()):
                lines.append(f'trinity_http_request_duration_seconds_sum{{method="{method}",path="{path}"}} {value:.6f}')
        return "\n".join(lines) + "\n"


metrics = RequestMetrics()
