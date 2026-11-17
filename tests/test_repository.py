from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]


def test_external_component_layout() -> None:
    required = [
        "components/t65_keypad/__init__.py",
        "components/t65_keypad/event.py",
        "components/t65_keypad/sensor.py",
        "components/t65_keypad/t65_keypad.h",
        "components/t65_keypad/t65_keypad.cpp",
        "components/t65_keypad/t65_keypad_state.h",
        "examples/esp32.yaml",
        "examples/esp32_s3.yaml",
        "examples/rotary_dial_replacement.yaml",
        "README.md",
        "LICENSE",
    ]
    missing = [path for path in required if not (ROOT / path).is_file()]
    assert not missing, f"Missing required repository files: {missing}"


def test_home_assistant_example_is_valid_yaml() -> None:
    with (ROOT / "examples/home_assistant.yaml").open(encoding="utf-8") as handle:
        document = yaml.safe_load(handle)
    assert document["automation"][0]["triggers"][0]["entity_id"] == (
        "event.t65_keypad"
    )
