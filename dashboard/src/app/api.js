// Calls to the FastAPI backend (see backend/README.md for the endpoints).
export const API_URL = (import.meta.env.VITE_API_URL || "http://localhost:8000").replace(/\/$/, "");

async function getJson(path) {
  const response = await fetch(`${API_URL}${path}`);
  if (!response.ok) {
    throw new Error(`${path} returned ${response.status}`);
  }
  return response.json();
}

export function fetchHealth() {
  return getJson("/health");
}

export function fetchLatest(deviceId) {
  const query = deviceId ? `?device_id=${encodeURIComponent(deviceId)}` : "";
  return getJson(`/api/v1/telemetry/latest${query}`);
}

/** Readings from the last `minutes`, returned oldest first for charting. */
export async function fetchHistory(deviceId, minutes, limit = 2000) {
  const params = new URLSearchParams({ limit: String(limit) });
  if (deviceId) params.set("device_id", deviceId);
  params.set("from", new Date(Date.now() - minutes * 60_000).toISOString());
  const rows = await getJson(`/api/v1/telemetry?${params}`);
  return rows.slice().reverse();
}
