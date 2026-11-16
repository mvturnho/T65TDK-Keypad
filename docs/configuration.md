# Configuration reference

## Component

```yaml
t65_keypad:
  id: t65
  rows: [GPIO19, GPIO21, GPIO22, GPIO23]
  columns: [GPIO16, GPIO17, GPIO18]
  debounce: 20ms
  on_key:
    - lambda: |-
        ESP_LOGI("t65", "Pressed: %c", x);
  on_key_release:
    - lambda: |-
        ESP_LOGI("t65", "Released: %c", x);
```

| Option | Required | Type/default | Meaning |
|---|---|---|---|
| `id` | No | generated ID | ID used by the Event platform and lambdas. |
| `rows` | Yes | exactly 4 GPIOs | R1 through R4, in keypad order. |
| `columns` | Yes | exactly 3 GPIOs | C1 through C3, in keypad order. |
| `debounce` | No | time, `20ms` | Stability interval; range `1ms`-`500ms`. |
| `on_key` | No | automation list | Runs once per valid stable press; `x` is `char`. |
| `on_key_release` | No | automation list | Runs after all contacts are stably released. |

Every GPIO must be unique, native to the ESP32, non-inverted, and capable of
`INPUT_PULLUP`. Do not set `allow_other_uses`; ESPHome's global pin validation
also prevents accidental sharing with another component.

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
`key_hash`. Only presses create entity events; releases remain local automation
callbacks.

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
