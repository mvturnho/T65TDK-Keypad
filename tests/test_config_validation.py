from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
COMPONENTS_DIR = (REPOSITORY_ROOT / "components").as_posix()


def configuration(
    *,
    rows: str = "[GPIO19, GPIO21, GPIO22, GPIO23]",
    columns: str = "[GPIO16, GPIO17, GPIO18]",
    debounce: str = "20ms",
    with_event: bool = False,
    keypad_extra: str = "",
    extra: str = "",
) -> str:
    event_config = ""
    if with_event:
        event_config = """
event:
  - platform: t65_keypad
    keypad_id: keypad
    name: Test Keypad
"""
    return f"""
esphome:
  name: t65-config-test
  min_version: 2026.10.2
esp32:
  board: esp32dev
  framework:
    type: esp-idf
external_components:
  - source:
      type: local
      path: {COMPONENTS_DIR}
    components: [t65_keypad]
logger:
t65_keypad:
  id: keypad
  rows: {rows}
  columns: {columns}
  debounce: {debounce}
{keypad_extra}
{event_config}
{extra}
"""


ROTARY_DIAL_FEATURES = """
  timeout: 4s
  on_numbers:
    - text_sensor.template.publish:
        id: numbers
        state: !lambda "return x;"
  on_key:
    - if:
        condition:
          lambda: "return x == '*';"
        then:
          - t65_keypad.clear_numbers: keypad
"""

ROTARY_DIAL_ENTITIES = """
sensor:
  - platform: t65_keypad
    keypad_id: keypad
    name: Test Dial
text_sensor:
  - platform: template
    id: numbers
    name: Test numbers
button:
  - platform: template
    name: Clear numbers
    on_press:
      - t65_keypad.clear_numbers:
          id: keypad
"""


def run_config(tmp_path: Path, yaml: str) -> subprocess.CompletedProcess[str]:
    config_path = tmp_path / "test.yaml"
    config_path.write_text(yaml, encoding="utf-8")
    return subprocess.run(
        [sys.executable, "-m", "esphome", "config", str(config_path)],
        check=False,
        capture_output=True,
        text=True,
    )


@pytest.mark.parametrize("with_event", [False, True])
def test_valid_configuration(tmp_path: Path, with_event: bool) -> None:
    result = run_config(tmp_path, configuration(with_event=with_event))
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("rows", "[GPIO19, GPIO21, GPIO22]"),
        ("columns", "[GPIO16, GPIO17]"),
    ],
)
def test_rejects_wrong_pin_count(tmp_path: Path, field: str, value: str) -> None:
    result = run_config(tmp_path, configuration(**{field: value}))
    assert result.returncode != 0
    assert "length" in (result.stdout + result.stderr).lower()


def test_rejects_duplicate_pin(tmp_path: Path) -> None:
    result = run_config(
        tmp_path, configuration(columns="[GPIO16, GPIO17, GPIO19]")
    )
    assert result.returncode != 0
    assert "assigned more than once" in result.stdout + result.stderr


def test_rejects_pin_without_internal_pullup(tmp_path: Path) -> None:
    result = run_config(
        tmp_path, configuration(columns="[GPIO16, GPIO17, GPIO34]")
    )
    assert result.returncode != 0
    assert "pullup" in (result.stdout + result.stderr).lower()


@pytest.mark.parametrize("debounce", ["0ms", "501ms"])
def test_rejects_invalid_debounce(tmp_path: Path, debounce: str) -> None:
    result = run_config(tmp_path, configuration(debounce=debounce))
    assert result.returncode != 0


def test_rejects_non_pullup_mode(tmp_path: Path) -> None:
    rows = """
    - GPIO19
    - GPIO21
    - GPIO22
    - number: GPIO23
      mode: INPUT
    """
    result = run_config(tmp_path, configuration(rows=rows))
    assert result.returncode != 0
    assert "INPUT_PULLUP" in result.stdout + result.stderr


def test_rotary_dial_features(tmp_path: Path) -> None:
    yaml = configuration(
        with_event=True,
        keypad_extra=ROTARY_DIAL_FEATURES,
        extra=ROTARY_DIAL_ENTITIES,
    )
    result = run_config(tmp_path, yaml)
    assert result.returncode == 0, result.stdout + result.stderr


def test_timeout_defaults_to_rotary_dial_value(tmp_path: Path) -> None:
    result = run_config(tmp_path, configuration())
    assert result.returncode == 0, result.stdout + result.stderr
    assert "timeout: 3s" in result.stdout


@pytest.mark.parametrize("timeout", ["99ms", "61s"])
def test_rejects_invalid_timeout(tmp_path: Path, timeout: str) -> None:
    result = run_config(tmp_path, configuration(keypad_extra=f"  timeout: {timeout}"))
    assert result.returncode != 0
    assert "timeout must be between" in result.stdout + result.stderr


def test_sensor_requires_keypad(tmp_path: Path) -> None:
    extra = """
sensor:
  - platform: t65_keypad
    keypad_id: missing_keypad
    name: Test Dial
"""
    result = run_config(tmp_path, configuration(extra=extra))
    assert result.returncode != 0
    assert "missing_keypad" in result.stdout + result.stderr


LONG_PRESS_FEATURES = """
  long_press_time: 800ms
  on_key_long_press:
    - lambda: |-
        ESP_LOGI("t65", "Long press: %c", x);
  repeat_time: 200ms
  on_key_repeat:
    - lambda: |-
        ESP_LOGI("t65", "Repeat: %c", x);
"""


def test_long_press_with_rotary_dial_features(tmp_path: Path) -> None:
    yaml = configuration(
        with_event=True,
        keypad_extra=ROTARY_DIAL_FEATURES + LONG_PRESS_FEATURES,
        extra=ROTARY_DIAL_ENTITIES,
    )
    result = run_config(tmp_path, yaml)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize(
    ("keypad_extra", "message"),
    [
        (
            """
  on_key_long_press:
    - logger.log: long
""",
            "on_key_long_press requires long_press_time",
        ),
        ("  repeat_time: 200ms", "repeat_time requires long_press_time"),
        (
            """
  long_press_time: 1s
  on_key_repeat:
    - logger.log: repeat
""",
            "on_key_repeat requires repeat_time",
        ),
    ],
)
def test_long_press_options_require_their_time(
    tmp_path: Path, keypad_extra: str, message: str
) -> None:
    result = run_config(tmp_path, configuration(keypad_extra=keypad_extra))
    assert result.returncode != 0
    assert message in result.stdout + result.stderr


@pytest.mark.parametrize("long_press_time", ["299ms", "11s"])
def test_rejects_invalid_long_press_time(tmp_path: Path, long_press_time: str) -> None:
    keypad_extra = f"  long_press_time: {long_press_time}"
    result = run_config(tmp_path, configuration(keypad_extra=keypad_extra))
    assert result.returncode != 0
    assert "long_press_time must be between" in result.stdout + result.stderr


@pytest.mark.parametrize("repeat_time", ["99ms", "11s"])
def test_rejects_invalid_repeat_time(tmp_path: Path, repeat_time: str) -> None:
    keypad_extra = f"""
  long_press_time: 1s
  repeat_time: {repeat_time}
"""
    result = run_config(tmp_path, configuration(keypad_extra=keypad_extra))
    assert result.returncode != 0
    assert "repeat_time must be between" in result.stdout + result.stderr


def generate_main_cpp(tmp_path: Path, yaml: str) -> str:
    config_path = tmp_path / "test.yaml"
    config_path.write_text(yaml, encoding="utf-8")
    result = subprocess.run(
        [sys.executable, "-m", "esphome", "compile", "--only-generate", str(config_path)],
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    main_cpp = tmp_path / ".esphome" / "build" / "t65-config-test" / "src" / "main.cpp"
    return main_cpp.read_text(encoding="utf-8")


@pytest.mark.parametrize(
    ("with_long_press", "with_repeat"), [(False, False), (True, False), (True, True)]
)
def test_long_press_event_types(
    tmp_path: Path, with_long_press: bool, with_repeat: bool
) -> None:
    keypad_extra = ""
    if with_long_press:
        keypad_extra += "  long_press_time: 1s\n"
    if with_repeat:
        keypad_extra += "  repeat_time: 250ms\n"
    main_cpp = generate_main_cpp(
        tmp_path, configuration(with_event=True, keypad_extra=keypad_extra)
    )
    assert '"key_hash"' in main_cpp
    assert ('"key_hash_long"' in main_cpp) == with_long_press
    assert ("set_long_press_time(1000)" in main_cpp) == with_long_press
    assert ('"key_hash_repeat"' in main_cpp) == with_repeat
    assert ("set_repeat_time(250)" in main_cpp) == with_repeat
