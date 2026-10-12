"""MQTT subscriber daemon (primary path).

Listens on coldchain/<device_id>/telemetry and stores every valid reading
through the same save_reading() used by the HTTPS endpoints.

Run:  python -m app.mqtt_subscriber   (from the backend/ folder)
"""
import json
import logging
import ssl

import paho.mqtt.client as mqtt
from pydantic import ValidationError

from . import config
from .db import TelemetryRepository
from .schemas import TelemetryPayload
from .service import save_reading

log = logging.getLogger("mqtt_subscriber")


def handle_message(raw: bytes, repo) -> bool:
    """Validate and store one MQTT message. Returns True when stored."""
    try:
        payload = TelemetryPayload.model_validate(json.loads(raw))
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        log.warning("Rejected message: not valid JSON (%s)", exc)
        return False
    except ValidationError as exc:
        log.warning("Rejected message: %s", exc.errors())
        return False
    if payload.telemetry.door is None and (
        payload.connection_type != "MQTT"
        or payload.device_id not in config.DOOR_OPTIONAL_DEVICE_IDS
    ):
        log.warning("Rejected MQTT message without door state from device %s", payload.device_id)
        return False
    row = save_reading(payload, repo)
    log.info("Stored reading id=%s device=%s temp=%s alert=%s",
             row["id"], row["device_id"], row["temperature"], row["is_alert"])
    return True


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    repo = TelemetryRepository()

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="coldchain-backend-subscriber")
    if config.MQTT_USERNAME:
        client.username_pw_set(config.MQTT_USERNAME, config.MQTT_PASSWORD)
    if config.MQTT_TLS:
        client.tls_set(ca_certs=config.MQTT_CA_CERT, tls_version=ssl.PROTOCOL_TLS_CLIENT)

    def on_connect(client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            log.error("MQTT connection failed: %s", reason_code)
            return
        log.info("Connected to %s:%s, subscribing to %s", config.MQTT_HOST, config.MQTT_PORT, config.MQTT_TOPIC)
        client.subscribe(config.MQTT_TOPIC, qos=1)

    def on_message(client, userdata, msg):
        try:
            handle_message(msg.payload, repo)
        except Exception:
            log.exception("Failed to store message from %s", msg.topic)

    client.on_connect = on_connect
    client.on_message = on_message
    client.reconnect_delay_set(min_delay=1, max_delay=30)
    client.connect(config.MQTT_HOST, config.MQTT_PORT, keepalive=60)
    client.loop_forever()


if __name__ == "__main__":
    main()
