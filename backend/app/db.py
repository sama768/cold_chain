"""PostgreSQL access for the telemetry table."""
from datetime import datetime
from typing import List, Optional

import psycopg
from psycopg.rows import dict_row

from . import config

COLUMNS = "id, device_id, temperature, humidity, pressure, battery_level, connection_type, is_alert, recorded_at"


class TelemetryRepository:
    def __init__(self, database_url: str = config.DATABASE_URL):
        self.database_url = database_url

    def _connect(self):
        return psycopg.connect(self.database_url, row_factory=dict_row)

    def insert(self, row: dict) -> dict:
        sql = f"""
            INSERT INTO telemetry
                (device_id, temperature, humidity, pressure, battery_level, connection_type, is_alert)
            VALUES
                (%(device_id)s, %(temperature)s, %(humidity)s, %(pressure)s,
                 %(battery_level)s, %(connection_type)s, %(is_alert)s)
            RETURNING {COLUMNS}
        """
        with self._connect() as conn:
            return conn.execute(sql, row).fetchone()

    def latest(self, device_id: Optional[str] = None) -> Optional[dict]:
        sql = f"SELECT {COLUMNS} FROM telemetry"
        params: dict = {}
        if device_id:
            sql += " WHERE device_id = %(device_id)s"
            params["device_id"] = device_id
        sql += " ORDER BY recorded_at DESC, id DESC LIMIT 1"
        with self._connect() as conn:
            return conn.execute(sql, params).fetchone()

    def history(
        self,
        device_id: Optional[str] = None,
        start: Optional[datetime] = None,
        end: Optional[datetime] = None,
        limit: int = 500,
    ) -> List[dict]:
        conditions, params = [], {"limit": limit}
        if device_id:
            conditions.append("device_id = %(device_id)s")
            params["device_id"] = device_id
        if start:
            conditions.append("recorded_at >= %(start)s")
            params["start"] = start
        if end:
            conditions.append("recorded_at <= %(end)s")
            params["end"] = end
        where = f"WHERE {' AND '.join(conditions)}" if conditions else ""
        sql = f"SELECT {COLUMNS} FROM telemetry {where} ORDER BY recorded_at DESC, id DESC LIMIT %(limit)s"
        with self._connect() as conn:
            return conn.execute(sql, params).fetchall()

    def ping(self) -> bool:
        with self._connect() as conn:
            conn.execute("SELECT 1")
        return True


_repo: Optional[TelemetryRepository] = None


def get_repo() -> TelemetryRepository:
    """FastAPI dependency. Tests replace it with a fake repository."""
    global _repo
    if _repo is None:
        _repo = TelemetryRepository()
    return _repo
