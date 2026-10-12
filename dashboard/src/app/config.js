// Dashboard settings. Keep the safe range in sync with TEMP_MIN_C / TEMP_MAX_C in backend/.env.
export const DEVICE_ID = "VAULT_FRIDGE_01";
export const DEVICE_NAME = "Refrigerator 1";

export const SAFE_MIN_C = 2.0;
export const SAFE_MAX_C = 8.0;

export const POLL_MS = 5000;
// The ESP32 reports every 5–30 s; no reading for 90 s means the device is probably offline.
export const STALE_AFTER_S = 90;

export const RANGES = [
  { label: "15 min", minutes: 15 },
  { label: "1 hour", minutes: 60 },
  { label: "6 hours", minutes: 360 },
  { label: "24 hours", minutes: 1440 },
];
