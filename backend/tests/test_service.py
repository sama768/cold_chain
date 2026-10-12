import pytest
from pydantic import ValidationError

from app.schemas import TelemetryPayload
from app.service import combined_temperature, is_out_of_range, save_reading, to_row


def test_combined_temperature_is_mean_of_both_sensors():
    assert combined_temperature(5.2, 5.4) == 5.3


def test_combined_temperature_uses_the_available_sensor():
    assert combined_temperature(None, 6.1) == 6.1
    assert combined_temperature(4.0, None) == 4.0


@pytest.mark.parametrize("temp, alert", [(1.9, True), (2.0, False), (5.0, False), (8.0, False), (8.1, True)])
def test_safe_range_is_2_to_8_degrees(temp, alert):
    assert is_out_of_range(temp) is alert


def test_sample_payload_maps_to_table_row(payload):
    row = to_row(TelemetryPayload.model_validate(payload))
    assert row == {
        "device_id": "VAULT_FRIDGE_01",
        "temperature": 5.3,
        "humidity": 48.5,
        "pressure": 1012.3,
        "battery_level": 98,
        "connection_type": "MQTT",
        "is_alert": False,
    }


def test_heat_excursion_sets_alert(payload, repo):
    payload["telemetry"]["temperature_dht"] = 9.2
    payload["telemetry"]["temperature_bmp"] = 9.6
    stored = save_reading(TelemetryPayload.model_validate(payload), repo)
    assert stored["is_alert"] is True
    assert stored["temperature"] == 9.4


@pytest.mark.parametrize(
    "path, value",
    [
        (("connection_type",), "LORA"),
        (("telemetry", "door"), "ajar"),
        (("telemetry", "humidity"), 140),
        (("telemetry", "temperature_dht"), 150),   # non-physical reading (spec 7.3)
        (("battery_level",), 101),
    ],
)
def test_invalid_values_are_rejected(payload, path, value):
    target = payload
    for key in path[:-1]:
        target = target[key]
    target[path[-1]] = value
    with pytest.raises(ValidationError):
        TelemetryPayload.model_validate(payload)


def test_reading_without_any_temperature_is_rejected(payload):
    payload["telemetry"]["temperature_dht"] = None
    payload["telemetry"]["temperature_bmp"] = None
    with pytest.raises(ValidationError):
        TelemetryPayload.model_validate(payload)


def test_empty_rfid_list_is_allowed(payload):
    payload["rfid"] = []
    assert TelemetryPayload.model_validate(payload).rfid == []
