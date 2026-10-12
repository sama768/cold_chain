import json

from app.mqtt_subscriber import handle_message


def test_valid_message_is_stored(repo, payload):
    assert handle_message(json.dumps(payload).encode(), repo) is True
    assert repo.rows[0]["connection_type"] == "MQTT"


def test_invalid_json_is_ignored(repo):
    assert handle_message(b"not json", repo) is False
    assert repo.rows == []


def test_invalid_payload_is_ignored(repo, payload):
    del payload["device_id"]
    assert handle_message(json.dumps(payload).encode(), repo) is False
    assert repo.rows == []
