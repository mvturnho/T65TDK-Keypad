# T65-TDK keypad for ESPHome

`t65_keypad` is an ESPHome external component for the original 12-key keypad
used in the Dutch PTT T65-TDK telephone. It reads the keypad's seven mechanical
selection contacts directly with an ESP32; the original DTMF generator is not
used.

Unlike a conventional key matrix, every key operates two electrically separate
contacts: one of four row contacts and one of three column contacts. Both
contact groups have their own common terminal. Connecting both commons to GND
allows all seven selection contacts to be read as active-low GPIO inputs with
internal pull-ups.

> [!WARNING]
> Electrically disconnect the keypad contacts from all original telephone and
> DTMF electronics before connecting an ESP32. Never apply telephone-line
> voltage to an ESP32 GPIO. Verify your own keypad with a multimeter; the pinout
> here was measured on one specific T65-TDK assembly and is not proof that every
> production variant is identical.

## Supported hardware and software

- Original 3-column by 4-row T65-TDK double-contact keypad.
- ESP32 and ESP32-S3 native GPIOs that support input, internal pull-up and
  interrupts.
- ESP-IDF (primary, compiled in CI) and Arduino on ESP32.
- ESPHome 2026.10.2 or newer. CI is pinned to 2026.10.2 for reproducibility.
- Home Assistant Core 2024.5 or newer when the optional Event entity is used.

No DTMF decoder, external supply, periodic scan task or ESPHome patch is needed.

The keypad is also a drop-in replacement for the `rotarydial` component used in
converted PTT T65 rotary telephones: it collects dialed numbers and offers the
same dial sensor, `on_numbers` automation and clear action. See
[rotary dial compatibility](#rotary-dial-compatibility).

## Wiring

| T65 pin | Contact | ESP32 example |
|---:|---|---|
| 1 | Column common | GND |
| 6 | C1 | GPIO16 |
| 7 | C2 | GPIO17 |
| 11 | C3 | GPIO18 |
| 9 | Row common | GND |
| 8 | R1 | GPIO19 |
| 10 | R2 | GPIO21 |
| 12 | R3 | GPIO22 |
| 13 | R4 | GPIO23 |

Pins 2 through 5 are unidentified and are not used by this component.

| | C1 | C2 | C3 |
|---|---:|---:|---:|
| R1 | 1 | 2 | 3 |
| R2 | 4 | 5 | 6 |
| R3 | 7 | 8 | 9 |
| R4 | `*` | 0 | `#` |

See [hardware details](docs/hardware.md) before wiring a keypad.

## Install from GitHub

Replace `OWNER` with the GitHub user or organization that hosts this repository:

```yaml
external_components:
  - source: github://OWNER/t65tdk-keypad@main
    components: [t65_keypad]
```

After a `v0.1.0` tag has actually been published, pin production devices to it:

```yaml
external_components:
  - source: github://OWNER/t65tdk-keypad@v0.1.0
    components: [t65_keypad]
```

No tag is created by this implementation. Do not use the pinned example until
that tag exists. To update later, change the ref deliberately, read the ESPHome
release notes, run `esphome config`, then compile before deploying.

## Minimal configuration

```yaml
t65_keypad:
  id: t65
  rows: [GPIO19, GPIO21, GPIO22, GPIO23]
  columns: [GPIO16, GPIO17, GPIO18]
  debounce: 20ms
  on_key:
    - lambda: |-
        ESP_LOGI("t65", "Key: %c", x);
  on_key_release:
    - lambda: |-
        ESP_LOGI("t65", "Released: %c", x);
```

`x` is a `char` containing `0`-`9`, `*`, or `#`. Local automations work with no
Home Assistant or network connection. GPIOs are configurable, but exactly four
unique row GPIOs and three unique column GPIOs are required. They are validated
as non-inverted native `INPUT_PULLUP` pins. Debounce defaults to `20ms` and may
be set from `1ms` through `500ms`.

Complete configurations are in [examples](examples/). For local development,
use the relative source shown in
[local_development.yaml](examples/local_development.yaml).

## Home Assistant Event entity

Add one optional event entity for the entire keypad:

```yaml
event:
  - platform: t65_keypad
    keypad_id: t65
    name: T65 Keypad
```

It exposes `key_0` through `key_9`, `key_star`, and `key_hash`, plus `_long`
and `_repeat` variants of each when [long press and repeat](#long-press-and-repeat)
are enabled. This is a native,
stateless Home Assistant **Event entity**. It is not a custom event fired on the
Home Assistant event bus. Automations normally watch the entity's state change
and inspect its `event_type` attribute. See the complete
[Home Assistant automation](examples/home_assistant.yaml) and ESPHome's
[Event documentation](https://esphome.io/components/event/index.html).

The entity is optional. `on_key` continues to work when Home Assistant is
offline, and event publication never participates in the ISR.

## Rotary dial compatibility

The keypad collects pressed keys into a number, the way the `rotarydial`
component collects dialed digits:

```yaml
t65_keypad:
  id: t65
  rows: [GPIO19, GPIO21, GPIO22, GPIO23]
  columns: [GPIO16, GPIO17, GPIO18]
  timeout: 3s
  on_numbers:
    - lambda: |-
        ESP_LOGI("t65", "Number: %s", x.c_str());

sensor:
  - platform: t65_keypad
    keypad_id: t65
    name: T65 Dial
```

`on_numbers` runs once no key has been pressed for `timeout` (default `3s`).
`x` is a `std::string` with every key since the last clear, such as
`0612345678` or `*21#`. Keys keep accumulating after the report until the
`t65_keypad.clear_numbers` action runs, normally on hang-up. The optional
sensor publishes each digit `0`-`9` as it is pressed, like the rotary dial
sensor.

To move an existing rotary dial configuration to the keypad without changing
Home Assistant, follow the [migration guide](docs/rotary_dial_migration.md) or
start from [rotary_dial_replacement.yaml](examples/rotary_dial_replacement.yaml).

## Long press and repeat

Long press and repeat are opt-in:

```yaml
t65_keypad:
  # ...
  long_press_time: 1s
  on_key_long_press:
    - lambda: |-
        ESP_LOGI("t65", "Long press: %c", x);
  repeat_time: 250ms
  on_key_repeat:
    - lambda: |-
        ESP_LOGI("t65", "Repeat: %c", x);
```

A key held for `long_press_time` runs `on_key_long_press` once, while it is
still held, and the Event entity reports `key_<key>_long`. Long presses are
left out of the collected number and the dial sensor; short presses are
collected when they are released. Normal dialing therefore reaches
`on_numbers` and the dial sensor exactly as it does with a rotary dial.
`on_key` and `on_key_release` still run for every press. Without
`long_press_time` nothing changes.

With `repeat_time`, a key that stays held after its long press runs
`on_key_repeat` every `repeat_time`, and the Event entity reports
`key_<key>_repeat`, until the key is released. Repeats are not collected
either. See [long press](docs/configuration.md#long-press) and
[repeat](docs/configuration.md#repeat) for the details.

## Runtime behavior

All seven inputs use any-edge interrupts. The ISR only increments an edge
counter and requests a normal ESPHome loop pass. GPIO snapshots, debounce,
decoding, logging, automations and event publication happen in normal context.
The component disables its loop whenever no debounce deadline or edge is
pending, so an idle keypad is not polled.

A key is reported only after exactly one row and one column remain stable for
the debounce interval. Holding does not repeat. Short contact interruptions do
not create release/press duplicates. Multiple or invalid contacts enter lockout
until every contact is stably released. A key held during boot is deliberately
suppressed and also requires a stable complete release before the keypad arms.

See [architecture](docs/architecture.md) for the state machine and ISR race
handling, and [configuration](docs/configuration.md) for every YAML option.

## Debugging

Normal presses and releases are logged at `INFO`. Stable masks and lockout
transitions are logged at `DEBUG`; edge snapshots are available at
`VERY_VERBOSE`. `dump_config()` reports all GPIOs, debounce, event presence and
the interrupt-driven/no-idle-polling mode. Start with
[troubleshooting](docs/troubleshooting.md) if codes or events are unexpected.

## Test status

The repository contains:

- native C++ tests for all 12 mappings, partial contacts, delayed closure,
  press/release bounce, hold, re-press, invalid combinations, second-key
  lockout, boot-held suppression, timer wrap, the loop-disable wake check,
  number collection, clearing and its length limit, short/long press
  classification including paused and non-repeated number reports, and
  repeat start, stop and lockout;
- real ESPHome schema tests for valid/eventless/event configurations, the
  rotary dial, long press and repeat features, and invalid counts,
  duplicates, GPIO capabilities, modes, debounce, timeout, long-press and
  repeat values and option dependencies, plus a code-generation check of the
  advertised long-press and repeat event types;
- GitHub Actions builds for ESP32 ESP-IDF, ESP32-S3 ESP-IDF, ESP32 Arduino and
  the rotary dial replacement example.

Software tests cannot verify contact quality, the measured physical pinout,
electrical isolation, ISR behavior under real hardware noise, or the exact GPIO
availability of every ESP32-S3 board variant. Those points require bench tests.

## License

MIT; see [LICENSE](LICENSE).
