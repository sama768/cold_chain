import { num, tempState, timeOfDay } from "../format.js";

/** The most recent readings, newest first. */
export default function ReadingsTable({ rows, count = 12 }) {
  const recent = rows.slice(-count).reverse();
  if (recent.length === 0) {
    return <p className="muted">No readings in this time range.</p>;
  }
  return (
    <div className="table-wrap">
      <table>
        <thead>
          <tr>
            <th scope="col">Time</th>
            <th scope="col" className="n">Temperature</th>
            <th scope="col" className="n">Humidity</th>
            <th scope="col" className="n">Pressure</th>
            <th scope="col">Link</th>
            <th scope="col">Status</th>
          </tr>
        </thead>
        <tbody>
          {recent.map((r) => {
            const state = tempState(r.temperature);
            return (
              <tr key={r.id} className={r.is_alert ? "row-alert" : undefined}>
                <td>{timeOfDay(r.recorded_at)}</td>
                <td className="n">{num(r.temperature)} °C</td>
                <td className="n">{num(r.humidity)} %</td>
                <td className="n">{num(r.pressure)} hPa</td>
                <td>{r.connection_type}</td>
                <td>
                  <span className={`tag is-${state}`}>
                    {state === "heat" ? "Too warm" : state === "cold" ? "Too cold" : "In range"}
                  </span>
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
}
