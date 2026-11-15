"""M31 trust pages: the four policies from config, honestly draft."""

from __future__ import annotations

from fastapi.testclient import TestClient

from src.main import app


def test_trust_lists_the_four_draft_policies():
    with TestClient(app) as client:
        r = client.get("/api/trust")
    assert r.status_code == 200, r.text
    body = r.json()

    slugs = [p["slug"] for p in body["policies"]]
    assert slugs == [
        "privacy",
        "terms",
        "refund-cancellation",
        "design-ownership",
    ]
    for policy in body["policies"]:
        assert policy["draft"] is True
        assert policy["note"] == "DRAFT - needs lawyer review"
        assert policy["version"] == body["policy_version"]
        assert policy["title"]
    assert body["company"]
    assert body["support_email"]
    assert body["policy_version"]
    # The dummy deployment ships refund copy; empty copy stays possible
    # (the frontend degrades honestly), but this deployment has it.
    refund = body["policies"][2]
    assert refund["text"]
