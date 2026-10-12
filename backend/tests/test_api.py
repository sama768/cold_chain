import pytest
from fastapi.testclient import TestClient

from app import config
from app.db import get_repo
from app.main import app

KEY = {"X-API-Key": config.DEVICE_API_KEY}


@pytest.fixture
def client(repo):
    app.dependency_overrides[get_repo] = lambda: repo
    yield TestClient(app)
    app.dependency_overrides.clear()


def test_health(client):
    assert client.get("/health").json() == {"status": "ok", "database": "ok"}


@pytest.mark.parametrize("path", ["/api/v1/telemetry", "/api/v1/telemetry/backup"])
def test_post_stores_reading(client, repo, payload, path):
    payload["connection_type"] = "HTTPS"
    response = client.post(path, json=payload, headers=KEY)
    assert response.status_code == 201
    body = response.json()
    assert body["id"] == 1
    assert body["connection_type"] == "HTTPS"
    assert float(body["temperature"]) == 5.3
    assert body["is_alert"] is False
    assert len(repo.rows) == 1


def test_backup_requires_api_key(client, repo, payload):
    assert client.post("/api/v1/telemetry/backup", json=payload).status_code == 401
    assert client.post("/api/v1/telemetry/backup", json=payload, headers={"X-API-Key": "wrong"}).status_code == 401
    assert repo.rows == []


def test_invalid_payload_returns_422(client, payload):
    payload["telemetry"]["door"] = "ajar"
    assert client.post("/api/v1/telemetry/backup", json=payload, headers=KEY).status_code == 422


def test_latest_and_history(client, payload):
    assert client.get("/api/v1/telemetry/latest").json() is None
    client.post("/api/v1/telemetry", json=payload, headers=KEY)
    payload["telemetry"]["temperature_dht"] = 9.0
    payload["telemetry"]["temperature_bmp"] = 9.0
    client.post("/api/v1/telemetry", json=payload, headers=KEY)

    latest = client.get("/api/v1/telemetry/latest").json()
    assert latest["id"] == 2 and latest["is_alert"] is True

    history = client.get("/api/v1/telemetry", params={"device_id": "VAULT_FRIDGE_01"}).json()
    assert [r["id"] for r in history] == [2, 1]

