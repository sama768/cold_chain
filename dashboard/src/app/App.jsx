import { useEffect, useState } from "react";
import { API_URL, fetchHistory, fetchLatest } from "./api.js";
import ReadingsTable from "./components/ReadingsTable.jsx";
import Thermometer from "./components/Thermometer.jsx";
import TrendChart from "./components/TrendChart.jsx";
import { DEVICE_ID, DEVICE_NAME, POLL_MS, RANGES, SAFE_MAX_C, SAFE_MIN_C, STALE_AFTER_S } from "./config.js";
import { ago, num, tempState } from "./format.js";
import { usePolling } from "./usePolling.js";

const VERDICT = {
  safe: { title: "Within the safe range", detail: `Storage temperature is between ${SAFE_MIN_C} and ${SAFE_MAX_C} °C.` },
  heat: { title: "Too warm", detail: `Above ${SAFE_MAX_C} °C. Check that the door is closed and the compressor is running.` },
  cold: { title: "Too cold", detail: `Below ${SAFE_MIN_C} °C. Vaccines can be damaged by freezing — check the thermostat.` },
  unknown: { title: "Waiting for data", detail: "No temperature has been received yet." },
};

function useNow(intervalMs = 1000) {
  const [now, setNow] = useState(Date.now());
  useEffect(() => {
    const id = setInterval(() => setNow(Date.now()), intervalMs);
    return () => clearInterval(id);
  }, [intervalMs]);
  return now;
}

function LinkStatus({ latest, error, now }) {
  let state = "live";
  let text = "Receiving data";
  if (error) {
    state = "down";
    text = "Backend unreachable";
  } else if (!latest) {
    state = "idle";
    text = "No readings yet";
  } else if ((now - new Date(latest.recorded_at).getTime()) / 1000 > STALE_AFTER_S) {
    state = "idle";
    text = "No recent data";
  }
  return (
    <span className={`link is-${state}`} role="status">
      <span className="link-dot" aria-hidden="true" />
      {text}
    </span>
  );
}

export default function App() {
  const [rangeMinutes, setRangeMinutes] = useState(60);
  const now = useNow();
  const { data: latest, error: latestError } = usePolling(() => fetchLatest(DEVICE_ID), POLL_MS);
  const { data: history, error: historyError } = usePolling(
    () => fetchHistory(DEVICE_ID, rangeMinutes),
    POLL_MS,
    [rangeMinutes],
  );

  const error = latestError || historyError;
  const rows = history || [];
  const state = tempState(latest?.temperature);
  const verdict = VERDICT[state];
  const alertsInRange = rows.filter((r) => r.is_alert).length;
  const loading = latest === undefined && !error;

  return (
    <div className="page">
      <header className="top">
        <div>
          <h1>{DEVICE_NAME}</h1>
          <p className="muted">Vaccine storage, device {DEVICE_ID}</p>
        </div>
        <LinkStatus latest={latest} error={error} now={now} />
      </header>

      {error && (
        <div className="notice is-error" role="alert">
          <strong>Can’t reach the backend at {API_URL}.</strong> Start it with{" "}
          <code>docker compose up</code> in the <code>backend</code> folder, or set <code>VITE_API_URL</code> in{" "}
          <code>.env</code> if it runs somewhere else.
        </div>
      )}

      {!error && latest === null && (
        <div className="notice">
          <strong>No readings stored yet.</strong> Send the sample reading from the <code>backend</code> folder:{" "}
          <code>curl -X POST {API_URL}/api/v1/telemetry -H "Content-Type: application/json" -H "X-API-Key: change-me" -d @sample_payload.json</code>
        </div>
      )}

      <section className={`hero is-${state}`} aria-busy={loading}>
        <Thermometer temperature={latest?.temperature} state={state} />
        <div className="hero-body">
          <p className="reading">
            <span className="reading-value">{num(latest?.temperature)}</span>
            <span className="reading-unit">°C</span>
          </p>
          <h2 className="verdict">{verdict.title}</h2>
          <p className="verdict-detail">{verdict.detail}</p>

          <dl className="facts">
            <div>
              <dt>Humidity</dt>
              <dd>{num(latest?.humidity)} %</dd>
            </div>
            <div>
              <dt>Pressure</dt>
              <dd>{num(latest?.pressure)} hPa</dd>
            </div>
            <div>
              <dt>Sent over</dt>
              <dd>{latest?.connection_type === "HTTPS" ? "HTTPS (backup)" : latest?.connection_type || "–"}</dd>
            </div>
            <div>
              <dt>Last reading</dt>
              <dd>{latest ? ago(latest.recorded_at, now) : "–"}</dd>
            </div>
            {latest?.battery_level !== null && latest?.battery_level !== undefined && (
              <div>
                <dt>Battery</dt>
                <dd>{latest.battery_level} %</dd>
              </div>
            )}
          </dl>
        </div>
      </section>

      <section className="panel">
        <div className="panel-head">
          <div>
            <h2>Temperature</h2>
            <p className="muted">
              {alertsInRange === 0
                ? "No readings outside the safe range in this period."
                : `${alertsInRange} reading${alertsInRange === 1 ? "" : "s"} outside the safe range in this period.`}
            </p>
          </div>
          <div className="range" role="group" aria-label="Time range">
            {RANGES.map((r) => (
              <button
                key={r.minutes}
                type="button"
                aria-pressed={rangeMinutes === r.minutes}
                onClick={() => setRangeMinutes(r.minutes)}
              >
                {r.label}
              </button>
            ))}
          </div>
        </div>
        <TrendChart rows={rows} field="temperature" unit="°C" label="Temperature" band={{ min: SAFE_MIN_C, max: SAFE_MAX_C }} />
      </section>

      <div className="pair">
        <section className="panel">
          <h2>Humidity</h2>
          <TrendChart rows={rows} field="humidity" unit="%" label="Humidity" height={160} />
        </section>
        <section className="panel">
          <h2>Pressure</h2>
          <TrendChart rows={rows} field="pressure" unit="hPa" label="Pressure" height={160} />
        </section>
      </div>

      <section className="panel">
        <h2>Recent readings</h2>
        <ReadingsTable rows={rows} />
      </section>

      <footer className="muted">
        Door status, shelf inventory and anomaly detection will appear here once the backend stores them.
      </footer>
    </div>
  );
}
