"""Telemetry processing shared by the MQTT subscriber and the HTTPS endpoints.

Both transports call save_reading(), so a reading is stored the same way
whichever path it arrived on.
"""
from typing import Optional

from . import config
from .schemas import TelemetryPayload


def combined_temperature(dht: Optional[float], bmp: Optional[float]) -> float:
    """Mean of the DHT22 and BMP280 temperatures, or the one that is available."""
    values = [t for t in (dht, bmp) if t is not None]
    return round(sum(values) / len(values), 2)


def is_out_of_range(temperature: float) -> bool:
    """True when the temperature is outside the safe storage range (2–8 °C)."""
    return temperature < config.TEMP_MIN_C or temperature > config.TEMP_MAX_C


def to_row(payload: TelemetryPayload) -> dict:
    """Convert a validated payload into the columns of the telemetry table."""
    t = payload.telemetry
    temperature = combined_temperature(t.temperature_dht, t.temperature_bmp)
    return {
        "device_id": payload.device_id,
        "temperature": temperature,
        "humidity": t.humidity,
        "pressure": t.pressure,
        "battery_level": payload.battery_level,
        "connection_type": payload.connection_type,
        "is_alert": is_out_of_range(temperature),
    }


def save_reading(payload: TelemetryPayload, repo) -> dict:
    """Process one reading and store it. Returns the stored row."""
    return repo.insert(to_row(payload))
