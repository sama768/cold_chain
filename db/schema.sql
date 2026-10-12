-- Smart Pharmaceutical Cold-Chain Warehouse
-- Phase 1 schema: telemetry table as defined in the system specification (section 6.2).
--
-- Loaded automatically by the PostgreSQL container on first start
-- (mounted into /docker-entrypoint-initdb.d by backend/docker-compose.yml).
-- To load it manually:  psql -U coldchain -d coldchain -f db/schema.sql

CREATE TABLE IF NOT EXISTS telemetry (
    id              BIGSERIAL     PRIMARY KEY,
    device_id       VARCHAR(64)   NOT NULL,
    temperature     NUMERIC(5,2),                 -- combined temperature: mean of DHT22 and BMP280 (°C)
    humidity        NUMERIC(5,2),                 -- relative humidity (%)
    pressure        NUMERIC(7,2),                 -- barometric pressure (hPa)
    battery_level   INTEGER       CHECK (battery_level BETWEEN 0 AND 100),
    connection_type VARCHAR(10)   NOT NULL CHECK (connection_type IN ('MQTT', 'HTTPS')),
    is_alert        BOOLEAN       NOT NULL DEFAULT FALSE,   -- TRUE when temperature is outside 2–8 °C
    recorded_at     TIMESTAMPTZ   NOT NULL DEFAULT now()    -- ingestion time
);

-- Dashboard queries read the newest readings of one device first.
CREATE INDEX IF NOT EXISTS ix_telemetry_device_recorded
    ON telemetry (device_id, recorded_at DESC);
