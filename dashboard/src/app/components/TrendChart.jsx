import { useEffect, useRef, useState } from "react";
import { num, timeOfDay } from "../format.js";

const PAD = { top: 12, right: 16, bottom: 26, left: 40 };

function useWidth() {
  const ref = useRef(null);
  const [width, setWidth] = useState(600);
  useEffect(() => {
    if (!ref.current) return;
    const observer = new ResizeObserver(([entry]) => setWidth(entry.contentRect.width));
    observer.observe(ref.current);
    return () => observer.disconnect();
  }, []);
  return [ref, width];
}

function niceTicks(min, max, count = 4) {
  const span = max - min || 1;
  const raw = span / count;
  const mag = 10 ** Math.floor(Math.log10(raw));
  const step = [1, 2, 2.5, 5, 10].map((m) => m * mag).find((s) => span / s <= count) || mag * 10;
  const ticks = [];
  for (let v = Math.ceil(min / step) * step; v <= max + 1e-9; v += step) ticks.push(Number(v.toFixed(6)));
  return ticks;
}

/**
 * Line chart of one field over time.
 * band: optional { min, max } drawn as the safe range; points outside it are marked.
 */
export default function TrendChart({ rows, field, unit, band, height = 220, label }) {
  const [ref, width] = useWidth();
  const [hover, setHover] = useState(null);

  const points = rows
    .filter((r) => r[field] !== null && r[field] !== undefined)
    .map((r) => ({ t: new Date(r.recorded_at).getTime(), v: Number(r[field]), row: r }));

  if (points.length < 2) {
    return (
      <div ref={ref} className="chart-empty" style={{ height }}>
        Not enough readings in this time range to draw a trend yet.
      </div>
    );
  }

  const values = points.map((p) => p.v);
  let vMin = Math.min(...values);
  let vMax = Math.max(...values);
  if (band) {
    vMin = Math.min(vMin, band.min - 1);
    vMax = Math.max(vMax, band.max + 1);
  }
  const vPad = (vMax - vMin) * 0.08 || 1;
  vMin -= vPad;
  vMax += vPad;
  const tMin = points[0].t;
  const tMax = points[points.length - 1].t;

  const innerW = Math.max(10, width - PAD.left - PAD.right);
  const innerH = height - PAD.top - PAD.bottom;
  const x = (t) => PAD.left + ((t - tMin) / (tMax - tMin || 1)) * innerW;
  const y = (v) => PAD.top + (1 - (v - vMin) / (vMax - vMin)) * innerH;

  const path = points.map((p, i) => `${i ? "L" : "M"}${x(p.t).toFixed(1)},${y(p.v).toFixed(1)}`).join("");
  const outside = band ? points.filter((p) => p.v > band.max || p.v < band.min) : [];
  // With a safe band, label whole 2-degree steps so the 2 °C and 8 °C limits appear on the axis.
  const yTicks = band
    ? Array.from({ length: 40 }, (_, i) => -20 + i * 2).filter((v) => v >= vMin && v <= vMax)
    : niceTicks(vMin, vMax);
  const xTicks = [0, 0.5, 1].map((f) => tMin + f * (tMax - tMin));

  function onMove(event) {
    const box = event.currentTarget.getBoundingClientRect();
    const px = event.clientX - box.left;
    let best = points[0];
    for (const p of points) if (Math.abs(x(p.t) - px) < Math.abs(x(best.t) - px)) best = p;
    setHover(best);
  }

  return (
    <div ref={ref} className="chart">
      <svg
        width={width}
        height={height}
        role="img"
        aria-label={`${label}: ${points.length} readings from ${timeOfDay(points[0].row.recorded_at)} to ${timeOfDay(
          points[points.length - 1].row.recorded_at,
        )}`}
        onMouseMove={onMove}
        onMouseLeave={() => setHover(null)}
      >
        {band && (
          <rect
            className="chart-band"
            x={PAD.left}
            width={innerW}
            y={y(band.max)}
            height={y(band.min) - y(band.max)}
          />
        )}
        {yTicks.map((v) => (
          <g key={v}>
            <line className="chart-grid" x1={PAD.left} x2={PAD.left + innerW} y1={y(v)} y2={y(v)} />
            <text className="chart-axis" x={PAD.left - 8} y={y(v) + 4} textAnchor="end">
              {v}
            </text>
          </g>
        ))}
        {xTicks.map((t, i) => (
          <text
            key={i}
            className="chart-axis"
            x={x(t)}
            y={height - 6}
            textAnchor={i === 0 ? "start" : i === 2 ? "end" : "middle"}
          >
            {new Date(t).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" })}
          </text>
        ))}
        <path className="chart-line" d={path} />
        {outside.map((p) => (
          <circle key={p.row.id} className="chart-alert" cx={x(p.t)} cy={y(p.v)} r="3.5" />
        ))}
        {hover && (
          <g>
            <line className="chart-cursor" x1={x(hover.t)} x2={x(hover.t)} y1={PAD.top} y2={PAD.top + innerH} />
            <circle className="chart-dot" cx={x(hover.t)} cy={y(hover.v)} r="4.5" />
          </g>
        )}
      </svg>
      {hover && (
        <div
          className="chart-tip"
          style={{ left: Math.min(Math.max(x(hover.t), 70), width - 70), top: Math.max(y(hover.v) - 52, 0) }}
        >
          <strong>
            {num(hover.v)} {unit}
          </strong>
          <span>{timeOfDay(hover.row.recorded_at)}</span>
        </div>
      )}
    </div>
  );
}
