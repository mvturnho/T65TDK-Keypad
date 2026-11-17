# Configuration reference

## Component

```yaml
t65_keypad:
  id: t65
  rows: [GPIO19, GPIO21, GPIO22, GPIO23]
  columns: [GPIO16, GPIO17, GPIO18]
  debounce: 20ms
  timeout: 3s
  long_press_time: 1s
  repeat_time: 250ms
  on_key:
    - lambda: |-
        ESP_LOGI("t65", "Pressed: %c", x);
  on_key_release:
    - lambda: |-
        ESP_LOGI("t65", "Released: %c", x);
  on_key_long_press:
    - lambda: |-
        ESP_LOGI("t65", "Long press: %c", x);
  on_key_repeat:
    - lambda: |-
        ESP_LOGI("t65", "Repeat: %c", x);
  on_numbers:
    - lambda: |-
        ESP_LOGI("t65", "Number: %s", x.c_str());
```

| Option | Required | Type/default | Meaning |
|---|---|---|---|
| `id` | No | generated ID | ID used by the Event platform and lambdas. |
| `rows` | Yes | exactly 4 GPIOs | R1 through R4, in keypad order. |
| `columns` | Yes | exactly 3 GPIOs | C1 through C3, in keypad order. |
| `debounce` | No | time, `20ms` | Stability interval; range `1ms`-`500ms`. |
| `on_key` | No | automation list | Runs once per valid stable press; `x` is `char`. |
| `on_key_release` | No | automation list | Runs after all contacts are stably released. |
| `timeout` | No | time, `3s` | Quiet time after the last key press before `on_numbers` runs; range `100ms`-`60s`. |
| `on_numbers` | No | automation list | Runs after `timeout`; `x` is a `std::string` with every key since the last clear. |
| `long_press_time` | No | time, not set | Enables long press; range `300ms`-`10s`. See [long press](#long-press). |
| `on_key_long_press` | No | automation list | Runs once when a key has been held for `long_press_time`; `x` is `char`. Requires `long_press_time`. |
| `repeat_time` | No | time, not set | Enables repeat after a long press; range `100ms`-`10s`. Requires `long_press_time`. See [repeat](#repeat). |
| `on_key_repeat` | No | automation list | Runs every `repeat_time` while a long-pressed key stays held; `x` is `char`. Requires `repeat_time`. |

Every GPIO must be unique, native to the ESP32, non-inverted, and capable of
`INPUT_PULLUP`. Do not set `allow_other_uses`; ESPHome's global pin validation
also prevents accidental sharing with another component.

## Collected number

Every accepted press is appended to a number of at most 32 keys, including
`*` and `#`. With [long press](#long-press) enabled, only short presses are
appended, at release. Each appended key restarts the `timeout`; when it
expires, `on_numbers` receives the whole number. The number is not cleared by reporting it, so
subsequent keys extend it, exactly like the `rotarydial` component. Start a
new number with the clear action:

```yaml
- t65_keypad.clear_numbers: t65
```

The longer form `t65_keypad.clear_numbers: {id: t65}` is also accepted, and
the ID may be omitted when there is only one keypad. Clearing also cancels an
`on_numbers` report that is still waiting for its timeout. Keys pressed while
the number is full are dropped and logged as a warning.

Lambdas can read the current number with `id(t65).get_numbers()`, which
returns a `std::string`. In `on_key` it already includes the key just pressed;
with long press enabled that is the case in `on_key_release` instead.

## Long press

Long press is off until `long_press_time` is set, and without it the keypad
behaves exactly as described above. With it, each accepted key becomes either
a short or a long press:

- **Short press**, released before `long_press_time`: classified at release.
  It is appended to the number, published on the dial sensor and reported as
  `key_<key>` by the Event entity.
- **Long press**, still held at `long_press_time`: classified at that moment,
  while the key is still down. `on_key_long_press` runs once and the Event
  entity reports `key_<key>_long`. The key is not appended to the number and
  does not update the dial sensor.

`on_key` and `on_key_release` are not classified: they still run on every
press and release, long or short. A key that is joined by another key before
`long_press_time` cannot become a long press; its release counts as a short
press, just as that press would have been collected without long press.

While a key is held, a pending `on_numbers` report waits until the key is
classified, so a number is never reported halfway through a key press. A long
press resumes a pending report with a fresh `timeout` but never repeats a
number that has already been reported.

This keeps the [rotary dial compatibility](rotary_dial_migration.md) intact:
dialing with short presses produces the same sensor values and `on_numbers`
strings, and a rotary dial also reports a digit only once the dial returns.

## Repeat

With `repeat_time` set, a key that is still held after its long press
repeats: `on_key_repeat` runs and the Event entity reports `key_<key>_repeat`,
first `repeat_time` after the long press and then every `repeat_time`, until
the key is released or joined by another key. For example, with
`long_press_time: 1s` and `repeat_time: 250ms`, holding a key produces its
long press at 1s and repeats at 1.25s, 1.5s, 1.75s and so on.

Repeats are never appended to the number and never update the dial sensor, so
they do not affect rotary dial compatibility either. Each repeat is scheduled
when the previous one has run, so the interval is `repeat_time` plus a few
milliseconds of loop latency rather than an exact clock.

A key that stays held keeps repeating. With the Event entity, each repeat is
an event sent to Home Assistant; choose a `repeat_time` that Home Assistant
automations can keep up with, and note that a stuck contact repeats until it
is freed.

## Dial sensor

```yaml
sensor:
  - platform: t65_keypad
    keypad_id: t65
    name: T65 Dial
```

Publishes each pressed digit as a number from `0` to `9`, with zero decimals,
matching the `rotarydial` sensor. With long press enabled it publishes short
presses at release and ignores long presses. `*` and `#` have no numeric value and do not
update it. All standard ESPHome sensor options, including filters, are
available. Home Assistant does not record a state change when the same value
is published again, so a repeated digit is invisible to state triggers there;
use `on_numbers` or the Event entity when every press matters.

## Event entity

```yaml
event:
  - platform: t65_keypad
    keypad_id: t65
    name: T65 Keypad
```

`keypad_id` is required and must reference a `t65_keypad` instance. All standard
ESPHome Event entity options, including `id`, `name`, `icon`, `internal`,
`disabled_by_default` and `entity_category`, are available. The device class
defaults to `button`.

The advertised event types are `key_0` through `key_9`, `key_star`, and
`key_hash`. When the keypad has `long_press_time`, the entity also advertises
`key_0_long` through `key_9_long`, `key_star_long` and `key_hash_long`, and a
press creates exactly one event: `key_<key>` for a short press at release, or
`key_<key>_long` for a long press. With `repeat_time`, the entity also
advertises `key_0_repeat` through `key_hash_repeat`, created for each repeat
of a held long press. Without long press every press creates `key_<key>`
immediately. Releases remain local automation callbacks.

## GitHub versus local source

Public repository:

```yaml
external_components:
  - source: github://OWNER/t65tdk-keypad@main
    components: [t65_keypad]
```

Repository checkout, with the ESPHome file in `examples/`:

```yaml
external_components:
  - source:
      type: local
      path: ../components
    components: [t65_keypad]
```

ESPHome's official external-component loader supports both forms. A release tag
should be preferred over `main` after a tag exists.

## Frameworks and boards

ESP-IDF is the primary framework and is compiled for `esp32dev` and
`esp32-s3-devkitc-1` in CI. Arduino uses the same ESPHome GPIO HAL and is also
compiled in CI. The S3 example uses GPIO4-7 and GPIO15-17; verify those against
the exact flash/PSRAM module and carrier schematic you own.
