import { useEffect, useRef, useState } from "react";

/**
 * Calls `load` now and then every `intervalMs`.
 * Phase 1 polls the REST API; this can be swapped for a WebSocket later.
 */
export function usePolling(load, intervalMs, deps = []) {
  const [data, setData] = useState(undefined);
  const [error, setError] = useState(null);
  const loadRef = useRef(load);
  loadRef.current = load;

  useEffect(() => {
    let cancelled = false;
    async function tick() {
      try {
        const result = await loadRef.current();
        if (!cancelled) {
          setData(result);
          setError(null);
        }
      } catch (err) {
        if (!cancelled) setError(err);
      }
    }
    tick();
    const id = setInterval(tick, intervalMs);
    return () => {
      cancelled = true;
      clearInterval(id);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [intervalMs, ...deps]);

  return { data, error };
}
