# Database

PostgreSQL storage for the cold-chain backend.

## Files

| File | Purpose |
| --- | --- |
| `schema.sql` | Creates the `telemetry` table (spec section 6.2) and its index |

## telemetry

| Column | Type | Description |
| --- | --- | --- |
| id | BIGSERIAL PRIMARY KEY | Auto-incrementing identifier |
| device_id | VARCHAR(64) | Hardware ID of the monitoring node, e.g. `VAULT_FRIDGE_01` |
| temperature | NUMERIC(5,2) | Combined temperature in °C: mean of DHT22 and BMP280 (or the one available) |
| humidity | NUMERIC(5,2) | Relative humidity in % |
| pressure | NUMERIC(7,2) | Barometric pressure in hPa |
| battery_level | INTEGER (0–100) | Remaining power, nullable |
| connection_type | VARCHAR(10) | Transport used: `MQTT` or `HTTPS` |
| is_alert | BOOLEAN | TRUE when temperature is outside the safe 2–8 °C range |
| recorded_at | TIMESTAMPTZ | Ingestion time (set by the database) |

## Loading the schema

With Docker (from `backend/`): `docker compose up -d db` loads `schema.sql` automatically the first
time the database volume is created. After changing the schema, recreate the volume:

```bash
docker compose down -v
docker compose up -d db
```

Without Docker:

```bash
createdb coldchain
psql -d coldchain -f db/schema.sql
```

## Planned (later phases)

RFID inventory, door events, alerts and ML inference tables will be added here once phase 1 works end to end.
