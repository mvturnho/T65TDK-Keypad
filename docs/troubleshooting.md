# Troubleshooting

## No keys are recognized

- Confirm T65 pin 1 and pin 9 both reach ESP32 GND.
- Measure continuity for each pressed row and column contact.
- Ensure the original DTMF/telephone circuit is completely disconnected.
- Check `dump_config()` for the intended row and column order.
- Use `VERY_VERBOSE` logging. A press should produce non-zero row and column
  masks; only one mask changing suggests a missing common or contact wire.

## Multiple events per press

Increase `debounce` gradually, for example to `30ms` or `50ms`. Verify that the
commons have a solid ground and that wires are not picking up interference.
The component does not repeat held keys; repeated events usually indicate a
real stable all-clear interval or incorrect wiring.

## Wrong key codes

Rows must be listed R1, R2, R3, R4 and columns C1, C2, C3. Reordering either
list changes the decoded position. Verify the physical mapping with a
multimeter rather than relying on wire colours.

## Keypad enters lockout

DEBUG logs report invalid masks and lockout. Look for two active rows or
columns, a stuck contact, conductive contamination or wiring that still joins
the keypad to legacy electronics. Lockout intentionally persists until all
seven inputs are stably inactive.

## GPIO conflict or pull-up error

Use seven unique native GPIOs. Input-only classic ESP32 pins GPIO34-39 do not
have internal pull-ups and are rejected. Avoid flash/PSRAM/USB/strapping and
onboard-peripheral pins even if the silicon itself supports input pull-up.

Do not configure `inverted: true` or override the mode: active-low conversion is
already part of the component and every pin must be `INPUT_PULLUP`.

## ESPHome cannot find the component

For GitHub, the repository must contain `components/t65_keypad/__init__.py` and
the source must name the correct owner/ref. For a local checkout, `path` must
point to the directory containing `t65_keypad`, normally this repository's
`components` directory, not to `components/t65_keypad` itself.

Use ESPHome 2026.10.2 or newer and run:

```console
esphome config your-device.yaml
esphome compile your-device.yaml
```

## Home Assistant Event entity is missing

- Confirm the `event:` block exists and references the right `keypad_id`.
- Home Assistant Core 2024.5 or newer is required for ESPHome Event entities.
- Reload or restart the ESPHome integration after first adding the entity.
- Inspect the entity in Developer Tools. Its `event_type` attribute, not an
  event-bus event name, identifies the key.
- Local `on_key` logging should still work if Home Assistant is unavailable;
  use that to separate keypad/wiring problems from API/entity problems.
