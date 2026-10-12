# Dashboard

## Purpose

Web interface for monitoring the pharmaceutical refrigerator.

## Displays

Phase 1 (working now, using the backend's telemetry endpoints):

- Current temperature on a thermometer scale with the 2–8 °C safe range, and a clear "in range / too warm / too cold" message
- Humidity, pressure, and whether the last reading came over MQTT or the HTTPS backup
- Communication status: receiving data, no recent data (nothing for 90 s), or backend unreachable
- Temperature, humidity and pressure history (15 min, 1 h, 6 h, 24 h) with out-of-range readings marked
- Table of the most recent readings

Later (needs backend support first):

- Door status
- RFID inventory
- Alerts list
- Anomaly score and incident category

## Technologies

- React 18 + Vite
- JavaScript
- Charts drawn as plain SVG (no chart library needed)
- Polls the REST API every 5 s; can be switched to WebSockets when the backend adds them

## Run

Needs Node.js 18+ and the backend running (see `backend/README.md`).

```bash
cd dashboard/src
npm install
npm run dev
```

Open http://localhost:5173.

The backend address defaults to `http://localhost:8000`. To change it, copy `.env.example` to `.env`
and edit `VITE_API_URL`.

## Files

| File | Purpose |
| --- | --- |
| `app/App.jsx` | Page layout and data loading |
| `app/components/Thermometer.jsx` | Thermometer scale with the safe range |
| `app/components/TrendChart.jsx` | History line chart |
| `app/components/ReadingsTable.jsx` | Recent readings table |
| `app/api.js` | Calls to the backend |
| `app/config.js` | Device ID, safe range, polling interval, time ranges |
| `app/styles.css` | Colours (light and dark mode) and layout |

If you change the safe range, update both `app/config.js` and `TEMP_MIN_C` / `TEMP_MAX_C` in the backend.
