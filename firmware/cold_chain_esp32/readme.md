# ESP32 Cold Chain Firmware

## 1. Project Overview

This firmware uses an ESP32 with three sensor modules:

- MFRC522 RFID reader
- DHT11 temperature and humidity sensor
- BMP180 temperature and pressure sensor

The ESP32 uses FreeRTOS to separate the work between its two CPU cores.

```text
ESP32
│
├── Core 0
│   └── Sensor Task
│       ├── RFID
│       ├── DHT11
│       └── BMP180
│
└── Core 1
    └── Network Task
        ├── MQTT/TLS QoS 1 publish
        ├── HTTPS fallback
        └── Bounded RAM retry queue
```

The network task publishes over MQTT/TLS first, falls back to authenticated HTTPS when
MQTT does not receive a QoS 1 broker acknowledgement, and buffers failed records in a
bounded RAM queue.

A matching MQTT `PUBACK` confirms receipt by the broker only; it does not confirm that the
backend subscriber validated or stored the message. Broker acceptance is the strongest
confirmation currently available on the device's MQTT path.

## HTTPS Fallback Integration Requirements

The firmware uses this backend endpoint for its HTTPS fallback:

```text
POST https://<backend-host>:8443/api/v1/telemetry/backup
X-API-Key: <DEVICE_API_KEY>
```

Copy `cold_chain_config.example.h` to `cold_chain_config.h` in this directory and configure
the Wi-Fi SSID/password, MQTT broker hostname/port, HTTPS backend hostname, and API key.
The local `cold_chain_config.h` is ignored by Git. Never commit production credentials.
Use a backend hostname or IP reachable from the ESP32; `localhost` on the device refers to
the ESP32 itself. The standalone backend HTTPS listener must be running on port `8443`.

The firmware uses `WiFiClientSecure` for both MQTT/TLS and HTTPS and requires separate
configured trust PEMs (`COLDCHAIN_MQTT_CA_CERT` and `COLDCHAIN_HTTPS_CA_CERT`). For local
development, the public contents of `backend/certs/dev_cert.pem` can be explicitly trusted
for HTTPS if it is a self-signed certificate; otherwise provision its issuing CA certificate.
For production and all normal device communication, provision the public CA chain that
issued the HTTPS certificate. The broker also needs a trusted CA certificate. Certificate
chain and hostname/SAN validation must remain enabled in development and production. Never
use `setInsecure()`, disable certificate verification, or copy the backend private key into
firmware.

The checked-in Mosquitto development configuration listens only on unencrypted port `1883`.
The firmware's secure MQTT default is `8883`; configure a TLS-enabled broker and trusted
broker CA separately before expecting the primary MQTT path to connect. Do not point
`WiFiClientSecure` at the plain development listener. Configure the backend subscriber with
`MQTT_TLS=true`, `MQTT_PORT=8883`, and its broker CA file (`MQTT_CA_CERT`) as well; otherwise
the broker may accept the ESP32 publish while the subscriber cannot consume it.

The JSON payload includes `device_id`, UTC `timestamp`, `connection_type`, and `telemetry`
with available temperatures, humidity, and pressure. The backup request
omits `door` rather than inventing a state; the backend accepts that omission only on the
backup route. For MQTT to be accepted by the backend subscriber without door state, add
that device ID to backend `DOOR_OPTIONAL_DEVICE_IDS` (for the example ID, set
`DOOR_OPTIONAL_DEVICE_IDS=VAULT_FRIDGE_01` in backend `.env`); all other MQTT devices still
require door state. The normal HTTP telemetry route requires door state. HTTPS sends
`X-API-Key` matching backend `DEVICE_API_KEY`; it is considered
delivered only for status `201` with a valid matching telemetry-record JSON response.

## Delivery and offline queue

Each network cycle tries MQTT/TLS QoS 1 first and requires a matching broker `PUBACK`.
If connection, TLS, authentication, publish, or acknowledgement fails, it tries the HTTPS
backup route. If both fail, the telemetry record is appended to a fixed eight-record RAM
queue. On later cycles the oldest queued record is retried before the new snapshot, and
is removed only after a MQTT `PUBACK` or validated HTTPS `201` response. A full queue is
reported to Serial; the new record cannot be retained and queued records are never
overwritten. Queue contents are lost on reset or power loss because this is RAM-only, not
durable storage. MQTT QoS 1 may redeliver if an acknowledgement is lost after broker receipt;
the backend does not currently deduplicate. A broker PUBACK does not prove backend storage.

The ESP32 also needs working DNS/NTP access for trustworthy UTC timestamps. If NTP time is
temporarily unavailable, sensor snapshots are buffered without a timestamp and are not sent.
When a valid UTC time becomes available, the timestamp is assigned before the queued record
is delivered. This is the first valid time available after capture, not the exact measurement
time. Such snapshots consume the same bounded queue capacity; if it fills, additional
readings are reported as unbuffered. The queue is not persistent across reset or power loss.

## Required device and backend configuration

Copy `cold_chain_config.example.h` to `cold_chain_config.h` and set:

- `COLDCHAIN_WIFI_SSID` and `COLDCHAIN_WIFI_PASSWORD` for the device's Wi-Fi network.
- `COLDCHAIN_DEVICE_ID` to the backend's device identity.
- `COLDCHAIN_MQTT_HOST`, `COLDCHAIN_MQTT_PORT`, credentials if required, and
  `COLDCHAIN_MQTT_CA_CERT` for a reachable TLS broker.
- `COLDCHAIN_HTTPS_HOST`, `COLDCHAIN_HTTPS_PORT` (8443), `COLDCHAIN_API_KEY`, and
  `COLDCHAIN_HTTPS_CA_CERT` for the backend HTTPS listener.
- `COLDCHAIN_NTP_SERVER` to a reachable NTP server.

Set backend `DEVICE_API_KEY` to exactly the same secret as `COLDCHAIN_API_KEY`. The firmware
publishes to `coldchain/<device_id>/telemetry`; backend `MQTT_TOPIC` must subscribe to a
matching topic (the default `coldchain/+/telemetry` does). The backend's `MQTT_HOST` and
`MQTT_PORT` must address the same broker as the firmware, though the hostnames can differ
when device and backend are on different networks. For TLS, set backend `MQTT_TLS=true`,
`MQTT_PORT` to the TLS listener (normally 8883), and `MQTT_CA_CERT` to its trusted CA path.
The example backend `.env` defaults to plaintext port 1883 and must be changed for a TLS
broker. Never make a production device connection using plaintext MQTT.

The HTTPS device hostname must resolve/reach the backend from the ESP32 and match the
certificate SAN. Do not use `localhost` unless the backend is actually on the device.
Provision only public trust certificates on the device. The backend private key stays on
the backend. The development certificate may require a matching hostname/certificate setup;
it is not a production CA certificate.

### Backend and network setup

From `backend/`, configure `ENABLE_HTTPS=true`, `HTTPS_PORT=8443`, `SSL_CERTFILE`,
`SSL_KEYFILE`, and `DEVICE_API_KEY` in `.env`, then run `python -m app.main` in one
terminal. In another terminal, the HTTP service remains on port `8000` when started with
`uvicorn app.main:app --host 0.0.0.0 --port 8000`. For a device on another network, set
`COLDCHAIN_HTTPS_HOST` to a routable backend address and arrange firewall/routing access to
8443. The current Docker Compose configuration does not publish HTTPS; use standalone
backend startup or a separately configured TLS reverse proxy.

---

# 2. FreeRTOS Architecture

## Core 0 — Sensor Task

Core 0 is responsible for reading all sensors.

The main task is:

```cpp
sensorTask()
```

It runs on:

```text
Core 0
```

The sensor task calls:

```cpp
readSensors()
```

which handles:

```cpp
readRFID()
readDHT()
readBMP()
```

The sensor task updates the shared:

```cpp
SensorData
```

structure.

### Core 0 responsibilities

```text
Read RFID
Read DHT11
Read BMP180
Update SensorData
Protect shared data with mutex
```

No network communication is performed inside the sensor functions.

---

# 3. Core 1 — Network Task

Core 1 runs:

```cpp
networkTask()
```

Core 1:

1. Reads the latest sensor data.
2. Copies the data safely using the mutex.
3. Attempts MQTT/TLS and checks for a QoS 1 `PUBACK`.
4. Attempts the HTTPS backup endpoint if MQTT delivery fails.
5. Retains undelivered records in the bounded RAM queue.

```text
Core 0
Sensor Task
     │
     │ SensorData
     ↓
Shared Data
     │
     │ mutex
     ↓
Core 1
Network Task
     │
     ├── MQTT/TLS → broker PUBACK
     ├── HTTPS → validated 201 response
     └── RAM queue → retry
```

The sensor-reading functions remain separate from network delivery.

---

# 4. Shared Sensor Data

The ESP32 uses:

```cpp
struct SensorData
```

The structure currently contains:

```text
RFID UID
Card Type
DHT Temperature
Humidity
BMP Temperature
Pressure
```

Example:

```text
RFID UID       → A3:7B:91:2F
Card Type      → MIFARE 1KB
DHT Temperature→ 5.4 °C
Humidity       → 62.0 %
BMP Temperature→ 5.8 °C
Pressure       → 101325 Pa
```

Core 0 writes the sensor data.

Core 1 reads a copy of the latest data.

A mutex called:

```cpp
dataMutex
```

protects the shared data.

---

# 5. Hardware

## Main Controller

```text
ESP32
```

The ESP32 provides:

- GPIO
- SPI
- I2C
- FreeRTOS
- Dual-core processing

---

# 6. MFRC522 RFID Reader

The RFID reader communicates with the ESP32 using SPI.

### Pin Connections

| MFRC522 | ESP32 |
|---|---:|
| SDA / SS | GPIO 5 |
| RST | GPIO 27 |
| SCK | GPIO 18 |
| MISO | GPIO 19 |
| MOSI | GPIO 23 |
| 3.3V | 3.3V |
| GND | GND |
| IRQ | Not connected |

### Voltage

```text
VCC: 3.3V
Logic: 3.3V
```

The MFRC522 is connected to the ESP32's 3.3V supply.

### RFID Data

When a tag is detected, the ESP32 reads:

```text
UID
Card Type
```

Example:

```text
RFID UID: A3:7B:91:2F
Card Type: MIFARE 1KB
```

---

# 7. DHT11

The DHT11 provides:

```text
Temperature
Humidity
```

### Pin Connections

| DHT11 | ESP32 |
|---|---:|
| DATA | GPIO 26 |
| VCC | 3.3V |
| GND | GND |

### Voltage

```text
VCC: 3.3V
```

### DHT11 Data

Example:

```text
Temperature: 5.4 °C
Humidity: 62.0 %
```

The DHT11 temperature is currently the main temperature value used in the shared sensor data.

---

# 8. BMP180

The BMP180 communicates with the ESP32 using I2C.

### Pin Connections

| BMP180 | ESP32 |
|---|---:|
| SDA | GPIO 21 |
| SCL | GPIO 22 |
| VCC | 3.3V |
| GND | GND |

### Voltage

```text
VCC: 3.3V
```

### BMP180 Data

The BMP180 provides:

```text
Temperature
Pressure
```

Example:

```text
BMP Temperature: 5.8 °C
Pressure: 101325 Pa
```

Pressure can also be displayed as:

```text
1013.25 hPa
```

---

# 9. Complete Pin Table

| Component | Pin | ESP32 |
|---|---|---:|
| MFRC522 | SDA / SS | GPIO 5 |
| MFRC522 | RST | GPIO 27 |
| MFRC522 | SCK | GPIO 18 |
| MFRC522 | MISO | GPIO 19 |
| MFRC522 | MOSI | GPIO 23 |
| MFRC522 | VCC | 3.3V |
| MFRC522 | GND | GND |
| DHT11 | DATA | GPIO 26 |
| DHT11 | VCC | 3.3V |
| DHT11 | GND | GND |
| BMP180 | SDA | GPIO 21 |
| BMP180 | SCL | GPIO 22 |
| BMP180 | VCC | 3.3V |
| BMP180 | GND | GND |

---

# 10. Communication Interfaces

The project currently uses two hardware communication interfaces.

## SPI

Used by:

```text
MFRC522
```

ESP32 SPI pins:

```text
SCK  → GPIO 18
MISO → GPIO 19
MOSI → GPIO 23
SS   → GPIO 5
```

RFID reset:

```text
RST → GPIO 27
```

## I2C

Used by:

```text
BMP180
```

ESP32 I2C pins:

```text
SDA → GPIO 21
SCL → GPIO 22
```

---

# 11. Sensor Data Flow

The complete current data flow is:

```text
                 ESP32
                   │
          ┌────────┴────────┐
          │                 │
       Core 0            Core 1
          │                 │
    Sensor Task        Network Task
          │                 │
     ┌────┼────┐            │
     │    │    │            │
   RFID DHT  BMP            │
     │    │    │            │
     └────┼────┘            │
          │                 │
          ↓                 │
     SensorData ────────────┘
                            │
                            ↓
                       Serial Monitor
```

---

# 12. Current Serial Output

The Core 1 task reports transport outcomes and queue status. For example:

```text
MQTT QoS 1 PUBACK was missing or invalid.
MQTT delivery failed; attempting verified HTTPS fallback.
Telemetry delivered via HTTPS fallback (201 response validated).
```

This means Core 1 is successfully receiving the latest sensor information from Core 0.

---

# 13. Current ESP32 Data

The current firmware produces the following sensor information:

| Data | Source | Current Status |
|---|---|---|
| RFID UID | MFRC522 | Available |
| Card Type | MFRC522 | Available |
| Temperature | DHT11 | Available |
| Humidity | DHT11 | Available |
| BMP Temperature | BMP180 | Available |
| Pressure | BMP180 | Available |

Example complete data:

```text
RFID UID: A3:7B:91:2F
Card Type: MIFARE 1KB

DHT11:
Temperature: 5.4 °C
Humidity: 62.0 %

BMP180:
Temperature: 5.8 °C
Pressure: 101325 Pa
```

---

# 14. Core Responsibilities

## Core 0

```text
Core 0
│
└── Sensor Task
    │
    ├── readRFID()
    ├── readDHT()
    ├── readBMP()
    │
    └── Update SensorData
```

Core 0 is responsible only for sensor acquisition.

## Core 1

```text
Core 1
│
└── Network Task
    │
    ├── Read SensorData
    └── Print data to Serial
```

Core 1 is responsible for processing shared data and telemetry transport.

---

# 15. Important Separation Rule

Keep the sensor and network responsibilities separate.

Do not put network communication inside:

```cpp
readRFID()
readDHT()
readBMP()
```

The sensor functions should only read sensors and update the sensor data.

The network task handles all transport and delivery retries.

Correct architecture:

```text
Core 0
│
├── RFID
├── DHT11
├── BMP180
│
└── SensorData
       │
       ↓
     Mutex
       │
       ↓
Core 1
│
└── Network Task
```

---

# 16. Current Status

### Implemented

```text
✓ ESP32
✓ FreeRTOS
✓ Core 0 Sensor Task
✓ Core 1 Network Task
✓ MFRC522 RFID
✓ DHT11
✓ BMP180
✓ SPI
✓ I2C
✓ Shared SensorData
✓ Mutex protection
✓ Serial output and transport status
✓ Wi-Fi connectivity
✓ MQTT/TLS QoS 1 publishing with PUBACK
✓ Verified HTTPS fallback
✓ Bounded RAM telemetry queue
```

### Not Implemented

```text
✗ Battery measurement
✗ Durable offline storage
```

---

# 17. Current Firmware Architecture

```text
                         ESP32
                           │
            ┌──────────────┴──────────────┐
            │                             │
         CORE 0                        CORE 1
            │                             │
      Sensor Task                    Network Task
            │                             │
     ┌──────┼──────┐                      │
     │      │      │                      │
   RFID   DHT11  BMP180                    │
     │      │      │                      │
     └──────┼──────┘                      │
            │                             │
            ↓                             │
       SensorData                          │
            │                             │
            └────────── Mutex ────────────┘
                                          │
                                          ↓
                                    Serial Output
```

The implementation keeps sensor acquisition separate from the Core 1 network task, which
publishes to MQTT/TLS, uses HTTPS as fallback, and retries from a bounded RAM queue.

# ESP32 Cold Chain Firmware

## 1. Project Overview

This firmware uses an ESP32 with three sensor modules:

- MFRC522 RFID reader
- DHT11 temperature and humidity sensor
- BMP180 temperature and pressure sensor

The ESP32 uses FreeRTOS to separate the work between its two CPU cores.

```text
ESP32
│
├── Core 0
│   └── Sensor Task
│       ├── RFID
│       ├── DHT11
│       └── BMP180
│
└── Core 1
    └── Network Task
        └── Reads SensorData
            ├── MQTT/TLS QoS 1
            ├── HTTPS fallback
            └── Bounded RAM queue
```

The network task implements Wi-Fi connectivity, MQTT/TLS publishing, authenticated HTTPS
fallback, and bounded in-memory retry for telemetry delivery.

---

# 2. FreeRTOS Architecture

## Core 0 — Sensor Task

Core 0 is responsible for reading all sensors.

The main task is:

```cpp
sensorTask()
```

It runs on:

```text
Core 0
```

The sensor task calls:

```cpp
readSensors()
```

which handles:

```cpp
readRFID()
readDHT()
readBMP()
```

The sensor task updates the shared:

```cpp
SensorData
```

structure.

### Core 0 responsibilities

```text
Read RFID
Read DHT11
Read BMP180
Update SensorData
Protect shared data with mutex
```

No network communication is performed inside the sensor functions.

---

# 3. Core 1 — Network Task

Core 1 runs:

```cpp
networkTask()
```

Core 1:

1. Reads the latest sensor data.
2. Copies the data safely using the mutex.
3. Attempts MQTT/TLS and checks for a QoS 1 `PUBACK`.
4. Attempts the HTTPS backup endpoint if MQTT delivery fails.
5. Retains undelivered records in the bounded RAM queue.

```text
Core 0
Sensor Task
     │
     │ SensorData
     ↓
Shared Data
     │
     │ mutex
     ↓
Core 1
Network Task
     │
     ├── MQTT/TLS → broker PUBACK
     ├── HTTPS → validated 201 response
     └── RAM queue → retry
```

The sensor-reading functions remain separate from network delivery.

---

# 4. Shared Sensor Data

The ESP32 uses:

```cpp
struct SensorData
```

The structure currently contains:

```text
RFID UID
Card Type
DHT Temperature
Humidity
BMP Temperature
Pressure
```

Example:

```text
RFID UID       → A3:7B:91:2F
Card Type      → MIFARE 1KB
DHT Temperature→ 5.4 °C
Humidity       → 62.0 %
BMP Temperature→ 5.8 °C
Pressure       → 101325 Pa
```

Core 0 writes the sensor data.

Core 1 reads a copy of the latest data.

A mutex called:

```cpp
dataMutex
```

protects the shared data.

---

# 5. Hardware

## Main Controller

```text
ESP32
```

The ESP32 provides:

- GPIO
- SPI
- I2C
- FreeRTOS
- Dual-core processing

---

# 6. MFRC522 RFID Reader

The RFID reader communicates with the ESP32 using SPI.

### Pin Connections

| MFRC522 | ESP32 |
|---|---:|
| SDA / SS | GPIO 5 |
| RST | GPIO 27 |
| SCK | GPIO 18 |
| MISO | GPIO 19 |
| MOSI | GPIO 23 |
| 3.3V | 3.3V |
| GND | GND |
| IRQ | Not connected |

### Voltage

```text
VCC: 3.3V
Logic: 3.3V
```

The MFRC522 is connected to the ESP32's 3.3V supply.

### RFID Data

When a tag is detected, the ESP32 reads:

```text
UID
Card Type
```

Example:

```text
RFID UID: A3:7B:91:2F
Card Type: MIFARE 1KB
```

---

# 7. DHT11

The DHT11 provides:

```text
Temperature
Humidity
```

### Pin Connections

| DHT11 | ESP32 |
|---|---:|
| DATA | GPIO 26 |
| VCC | 3.3V |
| GND | GND |

### Voltage

```text
VCC: 3.3V
```

### DHT11 Data

Example:

```text
Temperature: 5.4 °C
Humidity: 62.0 %
```

The DHT11 temperature is currently the main temperature value used in the shared sensor data.

---

# 8. BMP180

The BMP180 communicates with the ESP32 using I2C.

### Pin Connections

| BMP180 | ESP32 |
|---|---:|
| SDA | GPIO 21 |
| SCL | GPIO 22 |
| VCC | 3.3V |
| GND | GND |

### Voltage

```text
VCC: 3.3V
```

### BMP180 Data

The BMP180 provides:

```text
Temperature
Pressure
```

Example:

```text
BMP Temperature: 5.8 °C
Pressure: 101325 Pa
```

Pressure can also be displayed as:

```text
1013.25 hPa
```

---

# 9. Complete Pin Table

| Component | Pin | ESP32 |
|---|---|---:|
| MFRC522 | SDA / SS | GPIO 5 |
| MFRC522 | RST | GPIO 27 |
| MFRC522 | SCK | GPIO 18 |
| MFRC522 | MISO | GPIO 19 |
| MFRC522 | MOSI | GPIO 23 |
| MFRC522 | VCC | 3.3V |
| MFRC522 | GND | GND |
| DHT11 | DATA | GPIO 26 |
| DHT11 | VCC | 3.3V |
| DHT11 | GND | GND |
| BMP180 | SDA | GPIO 21 |
| BMP180 | SCL | GPIO 22 |
| BMP180 | VCC | 3.3V |
| BMP180 | GND | GND |

---

# 10. Communication Interfaces

The project currently uses two hardware communication interfaces.

## SPI

Used by:

```text
MFRC522
```

ESP32 SPI pins:

```text
SCK  → GPIO 18
MISO → GPIO 19
MOSI → GPIO 23
SS   → GPIO 5
```

RFID reset:

```text
RST → GPIO 27
```

## I2C

Used by:

```text
BMP180
```

ESP32 I2C pins:

```text
SDA → GPIO 21
SCL → GPIO 22
```

---

# 11. Sensor Data Flow

The complete current data flow is:

```text
                 ESP32
                   │
          ┌────────┴────────┐
          │                 │
       Core 0            Core 1
          │                 │
    Sensor Task        Network Task
          │                 │
     ┌────┼────┐            │
     │    │    │            │
   RFID DHT  BMP            │
     │    │    │            │
     └────┼────┘            │
          │                 │
          ↓                 │
     SensorData ────────────┘
                            │
                            ↓
                       MQTT/TLS
                           │
                    fallback HTTPS
                           │
                    bounded RAM queue
```

---

# 12. Current Serial Output

The Core 1 task reports transport outcomes and queue status. For example:

```text
MQTT QoS 1 PUBACK was missing or invalid.
MQTT delivery failed; attempting verified HTTPS fallback.
Telemetry delivered via HTTPS fallback (201 response validated).
```

This means Core 1 is successfully receiving the latest sensor information from Core 0.

---

# 13. Current ESP32 Data

The current firmware produces the following sensor information:

| Data | Source | Current Status |
|---|---|---|
| RFID UID | MFRC522 | Available |
| Card Type | MFRC522 | Available |
| Temperature | DHT11 | Available |
| Humidity | DHT11 | Available |
| BMP Temperature | BMP180 | Available |
| Pressure | BMP180 | Available |

Example complete data:

```text
RFID UID: A3:7B:91:2F
Card Type: MIFARE 1KB

DHT11:
Temperature: 5.4 °C
Humidity: 62.0 %

BMP180:
Temperature: 5.8 °C
Pressure: 101325 Pa
```

---

# 14. Core Responsibilities

## Core 0

```text
Core 0
│
└── Sensor Task
    │
    ├── readRFID()
    ├── readDHT()
    ├── readBMP()
    │
    └── Update SensorData
```

Core 0 is responsible only for sensor acquisition.

## Core 1

```text
Core 1
│
└── Network Task
    │
    ├── Read SensorData
    ├── Publish MQTT/TLS QoS 1
    ├── Fall back to HTTPS
    └── Retry queued telemetry
```

Core 1 is responsible for processing shared data and telemetry transport.

---

# 15. Important Separation Rule

Keep the sensor and network responsibilities separate.

Do not put network communication inside:

```cpp
readRFID()
readDHT()
readBMP()
```

The sensor functions should only read sensors and update the sensor data.

The network task handles all transport and delivery retries.

Correct architecture:

```text
Core 0
│
├── RFID
├── DHT11
├── BMP180
│
└── SensorData
       │
       ↓
     Mutex
       │
       ↓
Core 1
│
└── Network Task
```

---

# 16. Current Status

### Implemented

```text
✓ ESP32
✓ FreeRTOS
✓ Core 0 Sensor Task
✓ Core 1 Network Task
✓ MFRC522 RFID
✓ DHT11
✓ BMP180
✓ SPI
✓ I2C
✓ Shared SensorData
✓ Mutex protection
✓ Serial output and transport status
✓ Wi-Fi connectivity
✓ MQTT/TLS QoS 1 publishing with PUBACK
✓ Verified HTTPS fallback
✓ Bounded RAM telemetry queue
```

### Not Implemented

```text
✗ Battery measurement
✗ Durable offline storage
```

---

# 17. Current Firmware Architecture

```text
                         ESP32
                           │
            ┌──────────────┴──────────────┐
            │                             │
         CORE 0                        CORE 1
            │                             │
      Sensor Task                    Network Task
            │                             │
     ┌──────┼──────┐                      │
     │      │      │                      │
   RFID   DHT11  BMP180                    │
     │      │      │                      │
     └──────┼──────┘                      │
            │                             │
            ↓                             │
       SensorData                          │
            │                             │
            └────────── Mutex ────────────┘
                                          │
                                          ↓
                                    Serial Output
```

The network task reads the shared sensor snapshot and performs MQTT/TLS delivery, HTTPS
fallback, and bounded RAM-queue retries while sensor acquisition remains on the other core.
