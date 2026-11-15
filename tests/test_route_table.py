"""The advertised route table: paths as documented, no double prefixes.

The M27 reference routes had no tests, which is how a literal
"/api/projects/from-reference/..." path rode the router's own /api
prefix into registration as /api/api/... — reachable by no client. And
a decorator with no function body (the unimplemented
/api/projects/from-design stub) silently stacked onto the next route's
handler, so POST /api/projects/from-design executed the reference
listing. Both classes of mistake are locked out here.
"""

from __future__ import annotations

import src.api.routes as routes_module


def _route_paths() -> dict[str, tuple[str, ...]]:
    table: dict[str, tuple[str, ...]] = {}
    for route in routes_module.router.routes:
        path = getattr(route, "path", None)
        methods = getattr(route, "methods", None)
        if path and methods:
            table.setdefault(path, tuple(sorted(methods)))
    return table


def test_advertised_reference_routes_exist_with_the_documented_paths():
    table = _route_paths()
    assert table["/api/reference"] == ("GET",)
    assert table["/api/projects/from-reference/{design_id}"] == ("POST",)
    assert table["/api/reference/candidates/{design_id}"] == ("POST",)
    assert any(
        path.startswith("/api/match") for path in table
    ), "GET /api/match?tags= is advertised by work.md and README.md"


def test_no_route_double_prefixes_the_api_mount():
    # The router carries the /api prefix; a literal "/api/..." path in
    # a decorator registers an unreachable /api/api/... route.
    doubled = [
        path
        for path in _route_paths()
        if path.startswith("/api/api/")
    ]
    assert doubled == []


def test_unimplemented_from_design_stub_is_not_a_hidden_rewire():
    # The from-design stub has no implementation yet. Its decorator
    # used to dangle and steal the next route's handler, so the path
    # answered with the wrong endpoint. Until it is implemented, the
    # path must be absent rather than answering as something else.
    assert "/api/projects/from-design" not in _route_paths()
