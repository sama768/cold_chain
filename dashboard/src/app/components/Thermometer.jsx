import { SAFE_MAX_C, SAFE_MIN_C } from "../config.js";

const SCALE_MIN = -4;
const SCALE_MAX = 14;
const HEIGHT = 320;
const TOP = 12;
const BOTTOM = HEIGHT - 12;

const y = (t) => {
  const clamped = Math.min(SCALE_MAX, Math.max(SCALE_MIN, t));
  return BOTTOM - ((clamped - SCALE_MIN) / (SCALE_MAX - SCALE_MIN)) * (BOTTOM - TOP);
};

/** Vertical temperature scale with the safe band and the current reading. */
export default function Thermometer({ temperature, state }) {
  const ticks = [];
  for (let t = SCALE_MIN; t <= SCALE_MAX; t += 2) ticks.push(t);
  const hasValue = temperature !== null && temperature !== undefined;
  const markerY = hasValue ? y(Number(temperature)) : null;

  return (
    <svg
      className="thermo"
      viewBox={`0 0 120 ${HEIGHT}`}
      role="img"
      aria-label={
        hasValue
          ? `Temperature ${Number(temperature).toFixed(1)} °C on a scale with the safe range ${SAFE_MIN_C} to ${SAFE_MAX_C} °C`
          : "No temperature reading yet"
      }
    >
      <rect className="thermo-tube" x="44" y={TOP} width="20" height={BOTTOM - TOP} rx="10" />
      <rect
        className="thermo-band"
        x="44"
        y={y(SAFE_MAX_C)}
        width="20"
        height={y(SAFE_MIN_C) - y(SAFE_MAX_C)}
      />
      {ticks.map((t) => (
        <g key={t}>
          <line className="thermo-tick" x1="68" x2={t % 4 === 0 ? 80 : 74} y1={y(t)} y2={y(t)} />
          {t % 4 === 0 && (
            <text className="thermo-label" x="86" y={y(t) + 4}>
              {t}°
            </text>
          )}
        </g>
      ))}
      <text className="thermo-limit" x="38" y={y(SAFE_MAX_C) + 4} textAnchor="end">
        {SAFE_MAX_C}
      </text>
      <text className="thermo-limit" x="38" y={y(SAFE_MIN_C) + 4} textAnchor="end">
        {SAFE_MIN_C}
      </text>
      {hasValue && (
        <g className={`thermo-marker is-${state}`} style={{ transform: `translateY(${markerY}px)` }}>
          <line x1="36" x2="72" y1="0" y2="0" />
          <circle cx="54" cy="0" r="7" />
        </g>
      )}
    </svg>
  );
}
