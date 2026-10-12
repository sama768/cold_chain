# Backend — Phase 1

Receives telemetry from the ESP32 over MQTT (primary) or HTTPS (backup), stores it in PostgreSQL,
and serves it to the dashboard.

```text
ESP32 --MQTT--> Mosquitto --> mqtt_subscriber --+
      --HTTPS (backup)------> FastAPI ----------+--> save_reading() --> PostgreSQL --> FastAPI --> Dashboard
```

Both transports go through the same `save_reading()` in `app/service.py`, which:

1. validates the payload (`app/schemas.py`, spec section 6.1),
2. computes `temperature` = mean of `temperature_dht` and `temperature_bmp` (or the one available),
3. sets `is_alert` when the temperature is outside 2–8 °C,
4. inserts one row into the `telemetry` table (`../db/schema.sql`).

## Folder layout

```text
backend/
  app/
    main.py             FastAPI app (+ /health)
    routers/telemetry.py  telemetry endpoints
    mqtt_subscriber.py  MQTT listener daemon
    service.py          shared processing logic
    schemas.py          Pydantic models
    db.py               PostgreSQL queries
    config.py           settings from environment / .env
  tests/                pytest tests (no database needed)
  mosquitto/            broker config
  docker-compose.yml    db + mqtt + api + subscriber
  sample_payload.json   example ESP32 message
```

## Run with Docker (easiest)

```bash
cd backend
docker compose up --build
```

- API and docs: http://localhost:8000/docs
- PostgreSQL: localhost:**5433** (user/password/db: `coldchain`). Port 5433, not 5432, so it does not clash
  with a PostgreSQL installed on Windows.
- MQTT broker: localhost:1883

## Run the API outside Docker (database still in Docker)

Handy while developing, because `--reload` restarts the API when you save a file.

```bash
docker compose up -d db mqtt          # only the database and broker
.venv\Scripts\activate
uvicorn app.main:app --reload         # uses localhost:5433 by default
python -m app.mqtt_subscriber         # in another terminal
```

Do not also start the `api` container then, or both will try to use port 8000.

## Run the standalone HTTPS listener

The HTTPS listener is a separate Uvicorn process using the same FastAPI app and routes.
It defaults to `0.0.0.0:8443`; the existing HTTP listener remains independently available
on port 8000. To use both, start each command in a separate terminal from `backend/`.

Terminal 1 — HTTP:

```powershell
uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
```

Terminal 2 — HTTPS:

```powershell
python -m app.main
```

Before starting HTTPS, copy `.env.example` to `.env` (if not already done) and configure:

- `ENABLE_HTTPS=true`
- `HTTPS_HOST` and `HTTPS_PORT` (defaults: `0.0.0.0` and `8443`)
- `SSL_CERTFILE` and `SSL_KEYFILE` pointing to a matching PEM certificate/private key pair

Relative TLS file paths are resolved from the `backend/` directory. Missing or unreadable
files produce a startup error from the HTTPS launcher. `ENABLE_HTTPS=true` does not cause
`uvicorn app.main:app` to start TLS; that command remains HTTP-only and logs a reminder
to launch the HTTPS process separately. If HTTPS settings or certificates are missing,
the HTTPS process will fail; the independent HTTP process can still serve port 8000.

The bundled `certs/dev_cert.pem` and `certs/dev_key.pem` are development credentials, not
production certificates. Clients must trust the issuing certificate and connect using a
hostname covered by the certificate. For production, configure a certificate from a trusted
issuer or terminate TLS at a properly configured reverse proxy. Docker Compose currently
publishes only port 8000 and does not mount these certificate files; this standalone
HTTPS command is for running the backend directly, not through the Compose API service.
The `/api/v1/telemetry/backup` route accepts HTTPS telemetry without `telemetry.door` because
the current firmware has no door sensor. The normal `/api/v1/telemetry` route still requires
a valid door state. To allow MQTT telemetry without door state for a specific firmware node,
explicitly add its `device_id` to the comma-separated `DOOR_OPTIONAL_DEVICE_IDS` setting in
the backend `.env`; with the default empty allowlist, MQTT validation continues to require
door state for all devices. For the example firmware device, configure
`DOOR_OPTIONAL_DEVICE_IDS=VAULT_FRIDGE_01` (matching `COLDCHAIN_DEVICE_ID`).

## Run without Docker

Needs Python 3.10+, a running PostgreSQL and a running Mosquitto.

```bash
cd backend
python -m venv .venv
.venv\Scripts\activate            # Windows  (Linux/macOS: source .venv/bin/activate)
pip install -r requirements-dev.txt
copy .env.example .env            # Linux/macOS: cp .env.example .env
# edit .env: set DATABASE_URL to your own PostgreSQL, usually ...@localhost:5432/coldchain
psql -U postgres -c "CREATE USER coldchain WITH PASSWORD 'coldchain'"
psql -U postgres -c "CREATE DATABASE coldchain OWNER coldchain"
psql -U coldchain -d coldchain -f ../db/schema.sql

uvicorn app.main:app --reload            # terminal 1: API on port 8000
python -m app.mqtt_subscriber            # terminal 2: MQTT listener
```

## Try it

Send a reading over HTTPS (the backup path):

```bash
curl -X POST http://localhost:8000/api/v1/telemetry/backup \
  -H "Content-Type: application/json" -H "X-API-Key: change-me" \
  -d @sample_payload.json
```

Send a reading over MQTT:

```bash
mosquitto_pub -h localhost -t coldchain/VAULT_FRIDGE_01/telemetry -q 1 -f sample_payload.json
```

Read it back:

```bash
curl http://localhost:8000/api/v1/telemetry/latest
curl "http://localhost:8000/api/v1/telemetry?device_id=VAULT_FRIDGE_01&limit=50"
```

## Endpoints

| Method | Path | Auth | Purpose |
| --- | --- | --- | --- |
| GET | `/health` | — | API and database status |
| POST | `/api/v1/telemetry` | `X-API-Key` | Store a reading over HTTPS (also useful for testing) |
| POST | `/api/v1/telemetry/backup` | `X-API-Key` | Failover path used by the ESP32 when MQTT fails; returns 201 |
| GET | `/api/v1/telemetry/latest` | — | Most recent reading (`?device_id=` optional) |
| GET | `/api/v1/telemetry` | — | History, newest first (`?device_id=&from=&to=&limit=`) |

Errors: `401` wrong or missing API key, `422` invalid payload.

## MQTT

- Topic: `coldchain/<device_id>/telemetry`, QoS 1
- Payload: same JSON as the HTTPS endpoints (see `sample_payload.json`)
- Invalid messages are logged and skipped.
- Development uses plain MQTT on 1883. The spec requires TLS on 8883 — see `mosquitto/mosquitto.conf`
  and set `MQTT_TLS=true`, `MQTT_PORT=8883`, `MQTT_CA_CERT=...` in `.env`.
- The firmware publishes QoS 1 over TLS to port 8883; configure both the broker and this subscriber
  for TLS using the same broker CA and credentials before using that primary path.
- A matching broker `PUBACK` means the broker received the publish; it does not confirm that
  this subscriber validated or stored it. Use subscriber logs and backend storage monitoring
  to confirm processing. A device cannot automatically fall back to HTTPS for a message that
  the broker acknowledged but the subscriber later rejects or fails to store.

## Tests

```bash
pytest
```

The tests use an in-memory fake instead of PostgreSQL, so they run without Docker.

## Not in phase 1

RFID inventory tracking, door events, alerts table, WebSocket live updates, ML inference,
replay of readings buffered on the ESP32, and dashboard login.
