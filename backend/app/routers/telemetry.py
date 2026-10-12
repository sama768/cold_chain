"""Telemetry endpoints: ingestion (HTTPS) and read access for the dashboard."""
import hmac
from datetime import datetime
from typing import List, Optional

from fastapi import APIRouter, Depends, Header, HTTPException, Query, status

from .. import config
from ..db import get_repo
from ..schemas import TelemetryPayload, TelemetryRecord
from ..service import save_reading

router = APIRouter(prefix="/api/v1/telemetry", tags=["telemetry"])


def require_api_key(x_api_key: Optional[str] = Header(None)) -> None:
    """The ESP32 sends its key in the X-API-Key header."""
    if not x_api_key or not hmac.compare_digest(x_api_key, config.DEVICE_API_KEY):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Invalid or missing API key")


@router.post(
    "",
    response_model=TelemetryRecord,
    status_code=status.HTTP_201_CREATED,
    dependencies=[Depends(require_api_key)],
    summary="Receive telemetry over HTTPS (also handy for testing without MQTT)",
)
def post_telemetry(payload: TelemetryPayload, repo=Depends(get_repo)):
    if payload.telemetry.door is None:
        raise HTTPException(status_code=422, detail="door is required for this endpoint")
    return save_reading(payload, repo)


@router.post(
    "/backup",
    response_model=TelemetryRecord,
    status_code=status.HTTP_201_CREATED,
    dependencies=[Depends(require_api_key)],
    summary="Failover path: the ESP32 posts here when MQTT publishing fails",
)
def post_telemetry_backup(payload: TelemetryPayload, repo=Depends(get_repo)):
    return save_reading(payload, repo)


@router.get("/latest", response_model=Optional[TelemetryRecord], summary="Most recent reading")
def get_latest(device_id: Optional[str] = None, repo=Depends(get_repo)):
    return repo.latest(device_id)


@router.get("", response_model=List[TelemetryRecord], summary="Reading history, newest first")
def get_history(
    device_id: Optional[str] = None,
    start: Optional[datetime] = Query(None, alias="from"),
    end: Optional[datetime] = Query(None, alias="to"),
    limit: int = Query(500, ge=1, le=5000),
    repo=Depends(get_repo),
):
    return repo.history(device_id=device_id, start=start, end=end, limit=limit)
