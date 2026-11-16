import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation, pins
from esphome.const import (
    CONF_ID,
    CONF_INPUT,
    CONF_INVERTED,
    CONF_MODE,
    CONF_NUMBER,
    CONF_ON_KEY,
    CONF_OUTPUT,
    CONF_PULLUP,
    CONF_TRIGGER_ID,
)
from esphome.types import ConfigType

CODEOWNERS = []
DEPENDENCIES = ["esp32"]

CONF_ROWS = "rows"
CONF_COLUMNS = "columns"
CONF_DEBOUNCE = "debounce"
CONF_ON_KEY_RELEASE = "on_key_release"

MIN_DEBOUNCE_MS = 1
MAX_DEBOUNCE_MS = 500

t65_keypad_ns = cg.esphome_ns.namespace("t65_keypad")
T65Keypad = t65_keypad_ns.class_("T65Keypad", cg.Component)
T65KeyTrigger = t65_keypad_ns.class_(
    "T65KeyTrigger", automation.Trigger.template(cg.char)
)
T65KeyReleaseTrigger = t65_keypad_ns.class_(
    "T65KeyReleaseTrigger", automation.Trigger.template(cg.char)
)


def _validate_debounce(value):
    value = cv.positive_time_period_milliseconds(value)
    if not MIN_DEBOUNCE_MS <= value.total_milliseconds <= MAX_DEBOUNCE_MS:
        raise cv.Invalid(
            f"debounce must be between {MIN_DEBOUNCE_MS}ms and "
            f"{MAX_DEBOUNCE_MS}ms"
        )
    return value


def _validate_pin_mode(pin: ConfigType) -> ConfigType:
    mode = pin[CONF_MODE]
    if not mode.get(CONF_INPUT) or not mode.get(CONF_PULLUP):
        raise cv.Invalid("T65 keypad pins must use INPUT_PULLUP mode")
    if mode.get(CONF_OUTPUT):
        raise cv.Invalid("T65 keypad pins cannot be configured as outputs")
    if pin.get(CONF_INVERTED):
        raise cv.Invalid(
            "T65 keypad pins must not be inverted; active-low handling is built in"
        )
    return pin


T65_PIN_SCHEMA = cv.All(
    pins.internal_gpio_input_pullup_pin_schema,
    _validate_pin_mode,
)


def _validate_unique_pins(config: ConfigType) -> ConfigType:
    pins_by_number: dict[int, str] = {}
    for group in (CONF_ROWS, CONF_COLUMNS):
        for index, pin in enumerate(config[group]):
            number = pin[CONF_NUMBER]
            location = f"{group}[{index}]"
            if number in pins_by_number:
                raise cv.Invalid(
                    f"GPIO{number} is assigned more than once "
                    f"({pins_by_number[number]} and {location})"
                )
            pins_by_number[number] = location
    return config


CONFIG_SCHEMA = cv.All(
    cv.COMPONENT_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(T65Keypad),
            cv.Required(CONF_ROWS): cv.All(
                cv.ensure_list(T65_PIN_SCHEMA), cv.Length(min=4, max=4)
            ),
            cv.Required(CONF_COLUMNS): cv.All(
                cv.ensure_list(T65_PIN_SCHEMA), cv.Length(min=3, max=3)
            ),
            cv.Optional(CONF_DEBOUNCE, default="20ms"): _validate_debounce,
            cv.Optional(CONF_ON_KEY): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(T65KeyTrigger),
                }
            ),
            cv.Optional(CONF_ON_KEY_RELEASE): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                        T65KeyReleaseTrigger
                    ),
                }
            ),
        }
    ),
    _validate_unique_pins,
    cv.only_on_esp32,
)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    rows = [await cg.gpio_pin_expression(pin) for pin in config[CONF_ROWS]]
    columns = [await cg.gpio_pin_expression(pin) for pin in config[CONF_COLUMNS]]
    cg.add(var.set_rows(rows))
    cg.add(var.set_columns(columns))
    cg.add(var.set_debounce(config[CONF_DEBOUNCE]))

    for conf in config.get(CONF_ON_KEY, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.register_key_trigger(trigger))
        await automation.build_automation(trigger, [(cg.char, "x")], conf)

    for conf in config.get(CONF_ON_KEY_RELEASE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.register_key_release_trigger(trigger))
        await automation.build_automation(trigger, [(cg.char, "x")], conf)
