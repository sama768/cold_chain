# Smart Pharmaceutical Cold-Chain Warehouse

An IoT-based pharmaceutical cold-chain monitoring system designed to monitor refrigerator environmental conditions, track pharmaceutical products using RFID, detect abnormal conditions using machine learning, and provide real-time monitoring through a web dashboard.

## Features

* Real-time temperature and humidity monitoring
* Temperature measurement using DHT22 and BMP280
* Pressure monitoring using BMP280
* RFID-based pharmaceutical inventory tracking
* Door status monitoring using a reed switch
* MQTT over TLS as the primary communication protocol
* HTTPS as a communication fallback
* Offline data buffering when both communication methods fail
* PostgreSQL telemetry storage
* Machine learning anomaly detection
* Incident classification and recommended actions
* React monitoring dashboard

## System Architecture

```text
                    ┌──────────────────────┐
                    │      ESP32           │
                    │                      │
                    │ DHT22                │
                    │ BMP280               │
                    │ Reed Switch          │
                    │ RFID Reader 1        │
                    │ RFID Reader 2        │
                    └──────────┬───────────┘
                               │
                     MQTT over TLS
                               │
                               ▼
                    ┌──────────────────────┐
                    │       Backend        │
                    │       FastAPI        │
                    └──────────┬───────────┘
                               │
                    ┌──────────┴───────────┐
                    │                      │
                    ▼                      ▼
             ┌──────────────┐      ┌──────────────┐
             │ PostgreSQL   │      │ ML Detection │
             └──────────────┘      └──────────────┘
                    │                      │
                    └──────────┬───────────┘
                               ▼
                    ┌──────────────────────┐
                    │ React Dashboard      │
                    └──────────────────────┘
```

## Hardware

| Component       | Purpose                                  |
| --------------- | ---------------------------------------- |
| ESP32           | Main controller                          |
| DHT22           | Temperature and humidity                 |
| BMP280          | Temperature and pressure                 |
| MFRC522 #1      | Tracks pharmaceutical product on shelf 1 |
| MFRC522 #2      | Tracks pharmaceutical product on shelf 2 |
| RFID Tag #1     | Identifies product 1                     |
| RFID Tag #2     | Identifies product 2                     |
| Reed Switch     | Detects refrigerator door state          |
| 5V Power Supply | Powers the system                        |

## RFID Tracking

The refrigerator contains two shelves/zones.

Each pharmaceutical product has an RFID tag and is associated with one RFID reader.

```text
Shelf 1                    Shelf 2
┌───────────────┐          ┌───────────────┐
│ RFID Reader 1 │          │ RFID Reader 2 │
│      ↓        │          │      ↓        │
│   Vaccine A   │          │   Vaccine B   │
│   Tag A       │          │   Tag B       │
└───────────────┘          └───────────────┘
```

If a tag is no longer detected by its assigned reader, the system considers the corresponding product removed from the shelf.

## Communication

The system uses MQTT as the primary communication protocol.

### Primary

```text
ESP32 → MQTT over TLS → Backend
```

MQTT uses TCP port `8883`.

### Fallback

If MQTT publishing fails:

```text
ESP32 → HTTPS POST → Backend
```

The standalone development backend listener uses TCP port `8443` and accepts the fallback
at `POST /api/v1/telemetry/backup` with the `X-API-Key` header. Use a backend address
reachable from the ESP32, not `localhost`. The key must match `DEVICE_API_KEY` in the
backend configuration. The device must validate the backend certificate using a trusted
CA/server certificate; never disable TLS verification or embed the server private key in
firmware.

For direct local backend development, the standalone HTTPS listener defaults to port `8443`
while the existing HTTP listener remains on port `8000`. The `443` port is commonly used by
a production-facing TLS terminator or reverse proxy; it is not published by the current
backend Docker Compose setup.

The firmware implements MQTT/TLS delivery followed by authenticated HTTPS fallback and a
bounded RAM retry queue. See the firmware README for provisioning details. Its HTTPS backup
payload omits the unavailable door state; the backend permits this only for the backup route.
The current Mosquitto configuration listens only on plain MQTT port `1883`; a TLS MQTT
broker on port `8883` and its trusted CA must be configured separately for the primary path.
MQTT QoS 1 broker acknowledgement confirms broker receipt, not backend storage. The queue
may retry a publish after a lost acknowledgement, so duplicate readings are possible.

### Offline Mode

If both MQTT and HTTPS fail, up to eight telemetry records are held in a bounded RAM queue
and retried when connectivity returns. The queue is not persistent and is cleared by reset or
power loss; a full queue is reported and cannot retain additional readings.
If NTP is temporarily unavailable, captured sensor snapshots wait in this same queue without
a timestamp and are sent only after a valid UTC timestamp can be assigned. The timestamp then
reflects the first available synchronized time, not the exact capture time.

```text
MQTT/TLS → HTTPS → bounded RAM queue → retry on later network cycles
```

## Telemetry

Example telemetry message:

```json
{
  "device_id": "VAULT_FRIDGE_01",
  "timestamp": "2026-10-04T14:29:00Z",
  "connection_type": "MQTT",
  "battery_level": 98,
  "telemetry": {
    "temperature_dht": 5.2,
    "humidity": 48.5,
    "temperature_bmp": 5.4,
    "pressure": 1012.3,
    "door": "closed"
  },
  "rfid": [
    {
      "reader_id": 1,
      "tag_uid": "A1B2C3D4",
      "product": "Vaccine_A"
    },
    {
      "reader_id": 2,
      "tag_uid": "E5F6G7H8",
      "product": "Vaccine_B"
    }
  ]
}
```

## Machine Learning

The ML component analyzes historical telemetry to detect abnormal refrigerator behavior.

### Features

* DHT22 temperature
* BMP280 temperature
* Humidity
* Pressure
* Temperature difference between sensors
* Mean chamber temperature
* Temperature change over time
* Humidity-pressure covariance
* RFID inventory changes

### Sliding Window

The model processes telemetry using a sliding time window.

For example:

```text
Telemetry
   ↓
[t1][t2][t3]...[t12]
             ↓
        ML inference
             ↓
      anomaly detection
```

The window can contain approximately 12–60 timesteps depending on the telemetry sampling interval.

## Incident Categories

The system can classify incidents into:

* `NORMAL_OPERATION`
* `HEAT_EXCURSION`
* `FREEZING_EXCURSION`
* `DOOR_AJAR_SEAL_BREACH`
* `SENSOR_DRIFT_HARDWARE_FAULT`
* `UNAUTHORIZED_STOCK_REMOVAL`

## Backend

The backend is responsible for:

* Receiving telemetry
* Validating incoming data
* Processing MQTT messages
* Handling HTTPS fallback requests
* Storing telemetry in PostgreSQL
* Running ML inference
* Generating alerts
* Providing APIs for the dashboard

Technology:

* Python
* FastAPI
* Paho-MQTT
* Pydantic
* PostgreSQL

## Database

Main telemetry fields include:

```text
id
device_id
temperature
humidity
pressure
battery_level
connection_type
is_alert
recorded_at
```

## Dashboard

The React dashboard provides:

* Current refrigerator temperature
* Humidity
* Pressure
* Door status
* RFID inventory
* Historical sensor graphs
* Active alerts
* ML anomaly score
* Incident category
* Recommended action
* Communication status

## Project Structure

```text
smart-pharmaceutical-cold-chain/
│
├── README.md
│
├── firmware/
│   └── cold_chain_esp32/
│       └── cold_chain_esp32.ino
│
├── backend/
│
├── ml/
│
├── dashboard/
│
├── database/
│
├── docs/
│
├── docker-compose.yml
│
└── .gitignore
```

## Development Flow

```text
Sensors + RFID
      ↓
    ESP32
      ↓
 MQTT over TLS
      ↓
   FastAPI
      ↓
 PostgreSQL
      ↓
 ML Analysis
      ↓
 React Dashboard
```

If MQTT fails:

```text
ESP32
  ↓
HTTPS
  ↓
FastAPI
```

If both fail:

```text
ESP32
  ↓
Local Storage
  ↓
Retry
```

## Future Improvements

* Multiple refrigerators
* More RFID zones
* GPS/location tracking
* SMS/email alerts
* Advanced anomaly detection
* Predictive maintenance
* Cloud deployment
* User authentication
* Audit logs
* Pharmaceutical batch and expiration tracking
