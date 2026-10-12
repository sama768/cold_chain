# Machine Learning and Anomaly Detection

## Objective

Analyze streaming refrigerator telemetry to flag abnormal thermal behavior, distinguish likely root causes, and provide an actionable incident summary. ML output should support operator decisions; it does not replace the configured cold-storage limits or validated safety procedures.

## Input and Windowing

The target input is the device telemetry envelope described in the backend README, arriving every 5-30 seconds. Maintain a timestamp-aware rolling window of 12-60 samples. The actual duration depends on sampling cadence: at 5-second intervals, this is about 1-5 minutes; at 30-second intervals, it is about 6-30 minutes. Configure by elapsed time or resample consistently if a fixed-duration window is required.

## Feature Engineering

| Feature | Definition / purpose |
| --- | --- |
| Sensor temperature delta | `abs(temperature_dht - temperature_bmp)`; identifies disagreement, drift, or a sensor fault |
| Mean chamber temperature | `(temperature_dht + temperature_bmp) / 2` when both readings pass validation |
| Thermal velocity | Temperature change per elapsed time, estimated across the window; detects rapid warming/cooling |
| Humidity-pressure covariance | Joint humidity/pressure movement over the window; can help distinguish a door/seal event from isolated sensor noise |
| Inventory rate of change | Change in active RFID tag count and tag identities; unexpected removals need correlation with authorization events |

Retain timestamps and raw measurements alongside derived features so alerts can be audited and recalculated.

## Data Quality

- Forward-fill or linearly interpolate short gaps only when the missing interval is at most 30 seconds; raise `CRITICAL_DATA_MISSING` when the gap exceeds that limit.
- Flag non-physical values, including temperatures above 100 deg C, before normalization. If clipping is used for model stability, retain the original value and its quality flag.
- Validate sensor units, timestamps, and minimum sample counts before inference. Do not treat imputed data as a fresh physical measurement.

## Incident Categories

| Category | Intended signal |
| --- | --- |
| `NORMAL_OPERATION` | Estimated chamber temperature is within 2.0-8.0 deg C, sensor readings are aligned, and inventory is stable |
| `HEAT_EXCURSION` | Mean chamber temperature is above 8.0 deg C or trending toward/through the upper limit |
| `FREEZING_EXCURSION` | Mean chamber temperature is below 2.0 deg C |
| `DOOR_AJAR_SEAL_BREACH` | Rapid humidity rise and pressure change coincide with a temperature increase |
| `SENSOR_DRIFT_HARDWARE_FAULT` | Temperature delta between DHT22 and BMP exceeds a configured tolerance |
| `UNAUTHORIZED_STOCK_REMOVAL` | RFID inventory decreases without a corresponding authorization event |

Thresholds for sensor disagreement, trend rates, and confidence should be calibrated and documented before deployment. The 2.0-8.0 deg C range is the configured operating band, not a model-learned limit.

## Inference Output

The target response shape is:

```json
{
   "device_id": "VAULT_FRIDGE_01",
   "timestamp": 1727741361,
   "inference": {
      "is_anomaly": true,
      "anomaly_score": 0.885,
      "confidence": 0.94,
      "incident_category": "HEAT_EXCURSION",
      "root_cause_factors": [
         {
            "feature": "temperature_mean_c",
            "value": 9.4,
            "status": "ABOVE_UPPER_THRESHOLD"
         },
         {
            "feature": "humidity_rate_of_change",
            "value": 2.1,
            "status": "ELEVATED"
         }
      ],
      "recommended_action": "CRITICAL: Chamber temperature exceeds 8.0 deg C limit. Inspect refrigeration compressor and verify door closure."
   }
}
```

Use a consistent timestamp convention across telemetry and inference. Define whether anomaly score is normalized to 0-1 and whether confidence is calibrated before exposing either value to operators.

## Candidate Tooling and Evaluation

The system specification lists Python with NumPy, Pandas, and SciPy for data processing, and Scikit-learn, PyTorch, or TensorFlow Lite as candidate model libraries. Select one model/runtime after collecting representative normal and incident data. Evaluate false negatives, false alarms, detection delay, and per-category performance; keep a rule-based threshold check for the hard 2.0-8.0 deg C safety band.

## Current Repository Status

This directory currently contains only this README; there is no model implementation, training data, dependency manifest, or inference test yet. The features, categories, and response above are the proposed contract, not measured model performance.