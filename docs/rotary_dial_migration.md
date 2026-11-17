# Replacing the rotarydial component

The keypad can take the place of the `rotarydial` custom component (a rotary
dial read through a trigger pin and a pulse pin) in an existing ESPHome
telephone configuration. Home Assistant keeps the same entities and receives
the same values, so its automations need no changes.

A complete telephone configuration is in
[rotary_dial_replacement.yaml](../examples/rotary_dial_replacement.yaml).

## What stays the same

- A numeric sensor publishes each dialed digit, `0` through `9`, the moment it
  is dialed.
- `on_numbers` runs once no key has been pressed for `timeout`. `x` is a
  `std::string` with every key since the last clear, so
  `!lambda 'return x;'` works unchanged. The default timeout is still `3s`.
- Keys keep accumulating after `on_numbers` has run. Clear them with the clear
  action, normally when the handset is hung up.

## What changes in the YAML

| rotarydial | t65_keypad |
|---|---|
| `sensor:` `- platform: rotarydial` | a `t65_keypad:` block plus `sensor:` `- platform: t65_keypad` |
| `trigger_pin`, `pulse_pin` | `rows` (four GPIOs) and `columns` (three GPIOs) on `t65_keypad:` |
| `timeout` on the sensor | `timeout` on `t65_keypad:` |
| `on_numbers` on the sensor | `on_numbers` on `t65_keypad:` |
| `rotarydial.clear_numbers: rotarydial_id` | `t65_keypad.clear_numbers: t65` |
| `id(rotarydial_id).get_numbers()` returns `std::vector<int>` | `id(t65).get_numbers()` returns `std::string` |

Before:

```yaml
sensor:
  - platform: rotarydial
    id: rotarydial_id
    name: ${devicename} Dial
    timeout: 4s
    trigger_pin:
      number: GPIO17
      mode:
        input: true
    pulse_pin:
      number: GPIO16
      mode:
        input: true
    on_numbers:
      then:
        - text_sensor.template.publish:
            id: numbers
            state: !lambda 'return x;'
```

After:

```yaml
t65_keypad:
  id: t65
  rows: [GPIO19, GPIO21, GPIO22, GPIO23]
  columns: [GPIO16, GPIO17, GPIO32]
  timeout: 4s
  on_numbers:
    then:
      - text_sensor.template.publish:
          id: numbers
          state: !lambda 'return x;'

sensor:
  - platform: t65_keypad
    keypad_id: t65
    name: ${devicename} Dial
```

Then replace every `rotarydial.clear_numbers: rotarydial_id` with
`t65_keypad.clear_numbers: t65`, for example in the hang-up automation and in
the `clear_number` API action.

Keep the sensor `name` and the device `name` exactly as they were, and do not
add a `friendly_name`. Home Assistant identifies the sensor by its device and
name, so it then updates the existing dial sensor instead of creating a new
one. The `numbers` text sensor
is a template sensor in your own configuration and does not change at all.

## Choosing GPIOs

The rotary dial needed two GPIOs; the keypad needs seven, each with an
internal pull-up. GPIO34 to GPIO39 have no pull-up and are rejected. The
pins in the example avoid the pins the original PTT T65 configuration uses
for its LEDs, hook switch, earth button and audio. Check them against your
own configuration: an I2S amplifier or other peripherals may already use some
of them, and ESPHome refuses a GPIO that two components claim. See
[hardware](hardware.md) for wiring the keypad itself.

## Deliberate differences

- **`*` and `#`.** They are added to the number, so `x` can be `*21#`. The
  numeric sensor has no value for them and is not updated. The optional
  [Event entity](configuration.md#event-entity) reports them as `key_star`
  and `key_hash`.
- **Clearing cancels a pending report.** With the rotary dial, hanging up
  within the timeout still ran `on_numbers` afterwards, with an empty or
  partial number that overwrote the "Hangup" state. The keypad discards the
  pending report together with the number.
- **Numbers are limited to 32 keys.** Further keys are dropped, with a
  warning in the log, until the number is cleared.
- **The timeout must be between `100ms` and `60s`.** The lower bound catches
  typos such as `4ms` for `4s`.
- **The timeout restarts on every key press.** The rotary dial restarted it
  when the dial started to turn.

## Adding long press and repeat

A rotary dial has no long press or repeat, so these are purely additions. Set
`long_press_time` and use `on_key_long_press` or the Event entity's `_long`
event types; add `repeat_time` and `on_key_repeat` or the `_repeat` event
types to keep reacting while the key stays held. Long presses and repeats
never reach the dial sensor or `on_numbers`, and short presses still do, now
when the key is released, the same moment a rotary dial reports its digit.
Home Assistant automations built for the rotary dial keep working. See
[long press](configuration.md#long-press) and [repeat](configuration.md#repeat).

## Repeated digits

As with the rotary dial, dialing the same digit twice in a row publishes the
same sensor value twice, and Home Assistant does not record a state change for
the second one. Use `on_numbers`, or the Event entity, which reports every
press, when each key matters.
