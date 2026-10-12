import copy
import json
from datetime import datetime, timezone
from pathlib import Path

import pytest

SAMPLE = json.loads((Path(__file__).resolve().parent.parent / "sample_payload.json").read_text())


class FakeRepo:
    """In-memory stand-in for TelemetryRepository, so tests need no database."""

    def __init__(self):
        self.rows = []

    def insert(self, row):
        stored = {**row, "id": len(self.rows) + 1, "recorded_at": datetime.now(timezone.utc)}
        self.rows.append(stored)
        return stored

    def latest(self, device_id=None):
        rows = [r for r in self.rows if device_id in (None, r["device_id"])]
        return rows[-1] if rows else None

    def history(self, device_id=None, start=None, end=None, limit=500):
        rows = [r for r in self.rows if device_id in (None, r["device_id"])]
        return list(reversed(rows))[:limit]

    def ping(self):
        return True


@pytest.fixture
def payload():
    return copy.deepcopy(SAMPLE)


@pytest.fixture
def repo():
    return FakeRepo()
