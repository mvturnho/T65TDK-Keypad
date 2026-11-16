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
{event_config}
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
