"""Settings read from environment variables (or backend/.env)."""
import os
from pathlib import Path


def _load_dotenv() -> None:
    """Load backend/.env if it exists. Real environment variables take priority."""
    env_file = Path(__file__).resolve().parent.parent / ".env"
    if not env_file.exists():
        return
    for line in env_file.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


_load_dotenv()

BACKEND_ROOT = Path(__file__).resolve().parent.parent


def _resolve_tls_path(path: str | None) -> str | None:
    """Resolve relative certificate paths against the backend directory."""
    if not path:
        return None
    if os.path.isabs(path):
        return path
    return str((BACKEND_ROOT / path).resolve())

DATABASE_URL = os.getenv("DATABASE_URL", "postgresql://coldchain:coldchain@localhost:5433/coldchain")

# API key the ESP32 sends in the X-API-Key header on HTTPS requests.
DEVICE_API_KEY = os.getenv("DEVICE_API_KEY", "change-me")

# Only explicitly listed devices may omit door state in MQTT telemetry.
DOOR_OPTIONAL_DEVICE_IDS = {
    device_id.strip()
    for device_id in os.getenv("DOOR_OPTIONAL_DEVICE_IDS", "").split(",")
    if device_id.strip()
}

# Safe storage range in °C (spec: 2.0–8.0 °C).
TEMP_MIN_C = float(os.getenv("TEMP_MIN_C", "2.0"))
TEMP_MAX_C = float(os.getenv("TEMP_MAX_C", "8.0"))

# MQTT subscriber
MQTT_HOST = os.getenv("MQTT_HOST", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))          # 8883 when TLS is enabled
MQTT_TOPIC = os.getenv("MQTT_TOPIC", "coldchain/+/telemetry")
MQTT_USERNAME = os.getenv("MQTT_USERNAME") or None
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD") or None
MQTT_TLS = os.getenv("MQTT_TLS", "false").lower() == "true"
MQTT_CA_CERT = os.getenv("MQTT_CA_CERT") or None          # path to the broker CA certificate

# Optional separate HTTPS listener for the same FastAPI app.
# Keep the existing HTTP API on port 8000 and use a distinct TLS port for dev testing.
ENABLE_HTTPS = os.getenv("ENABLE_HTTPS", "false").lower() == "true"
HTTPS_HOST = os.getenv("HTTPS_HOST", "0.0.0.0")
HTTPS_PORT = int(os.getenv("HTTPS_PORT", "8443"))
SSL_CERTFILE = _resolve_tls_path(os.getenv("SSL_CERTFILE")) or None
SSL_KEYFILE = _resolve_tls_path(os.getenv("SSL_KEYFILE")) or None
