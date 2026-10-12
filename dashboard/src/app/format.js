import { SAFE_MAX_C, SAFE_MIN_C } from "./config.js";

export function num(value, digits = 1) {
  return value === null || value === undefined ? "–" : Number(value).toFixed(digits);
}

export function timeOfDay(iso) {
  return new Date(iso).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
}

export function ago(iso, now = Date.now()) {
  const seconds = Math.max(0, Math.round((now - new Date(iso).getTime()) / 1000));
  if (seconds < 60) return `${seconds} s ago`;
  const minutes = Math.round(seconds / 60);
  if (minutes < 60) return `${minutes} min ago`;
  const hours = Math.round(minutes / 60);
  if (hours < 48) return `${hours} h ago`;
  return new Date(iso).toLocaleDateString();
}

/** "safe" | "heat" | "cold" for a temperature in °C. */
export function tempState(t) {
  if (t === null || t === undefined) return "unknown";
  if (t > SAFE_MAX_C) return "heat";
  if (t < SAFE_MIN_C) return "cold";
  return "safe";
}
