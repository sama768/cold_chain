# API Documentation

## POST /api/v1/telemetry

Receives normal telemetry.

## POST /api/v1/telemetry/backup

Receives telemetry when MQTT fails. Requires the `X-API-Key` header matching
backend `DEVICE_API_KEY`, accepts `201 Created` with a telemetry record, and permits
the firmware's payload to omit `telemetry.door` because no door sensor is wired.
The regular `POST /api/v1/telemetry` endpoint still requires a door value. MQTT payloads
without a door value are accepted only when their `device_id` is explicitly listed in
backend `DOOR_OPTIONAL_DEVICE_IDS`.

## GET /api/v1/telemetry

Returns historical telemetry.

## GET /api/v1/inventory

Returns current RFID inventory.

## GET /api/v1/alerts

Returns detected alerts.