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
    CONF_TIMEOUT,
    CONF_TRIGGER_ID,
)
from esphome.types import ConfigType

CODEOWNERS = []
DEPENDENCIES = ["esp32"]

CONF_ROWS = "rows"
CONF_COLUMNS = "columns"
CONF_DEBOUNCE = "debounce"
CONF_ON_KEY_RELEASE = "on_key_release"
CONF_ON_KEY_LONG_PRESS = "on_key_long_press"
CONF_LONG_PRESS_TIME = "long_press_time"
CONF_ON_KEY_REPEAT = "on_key_repeat"
CONF_REPEAT_TIME = "repeat_time"
CONF_ON_NUMBERS = "on_numbers"
CONF_KEYPAD_ID = "keypad_id"

MIN_DEBOUNCE_MS = 1
MAX_DEBOUNCE_MS = 500
# The lower bound catches typos such as "3ms" for "3s".
MIN_TIMEOUT_MS = 100
MAX_TIMEOUT_MS = 60_000
MIN_LONG_PRESS_MS = 300
MAX_LONG_PRESS_MS = 10_000
MIN_REPEAT_MS = 100
MAX_REPEAT_MS = 10_000

t65_keypad_ns = cg.esphome_ns.namespace("t65_keypad")
T65Keypad = t65_keypad_ns.class_("T65Keypad", cg.Component)
T65KeyTrigger = t65_keypad_ns.class_(
    "T65KeyTrigger", automation.Trigger.template(cg.char)
)
T65KeyReleaseTrigger = t65_keypad_ns.class_(
    "T65KeyReleaseTrigger", automation.Trigger.template(cg.char)
)
T65KeyLongPressTrigger = t65_keypad_ns.class_(
    "T65KeyLongPressTrigger", automation.Trigger.template(cg.char)
)
T65KeyRepeatTrigger = t65_keypad_ns.class_(
    "T65KeyRepeatTrigger", automation.Trigger.template(cg.char)
)
T65NumbersTrigger = t65_keypad_ns.class_(
    "T65NumbersTrigger", automation.Trigger.template(cg.std_string)
)
T65ClearNumbersAction = t65_keypad_ns.class_(
    "T65ClearNumbersAction", automation.Action, cg.Parented.template(T65Keypad)
)


def _validate_debounce(value):
    value = cv.positive_time_period_milliseconds(value)
    if not MIN_DEBOUNCE_MS <= value.total_milliseconds <= MAX_DEBOUNCE_MS:
        raise cv.Invalid(
            f"debounce must be between {MIN_DEBOUNCE_MS}ms and "
            f"{MAX_DEBOUNCE_MS}ms"
        )
    return value


def _validate_timeout(value):
    value = cv.positive_time_period_milliseconds(value)
    if not MIN_TIMEOUT_MS <= value.total_milliseconds <= MAX_TIMEOUT_MS:
        raise cv.Invalid(
            f"timeout must be between {MIN_TIMEOUT_MS}ms and {MAX_TIMEOUT_MS // 1000}s"
        )
    return value


def _validate_long_press_time(value):
    value = cv.positive_time_period_milliseconds(value)
    if not MIN_LONG_PRESS_MS <= value.total_milliseconds <= MAX_LONG_PRESS_MS:
        raise cv.Invalid(
            f"long_press_time must be between {MIN_LONG_PRESS_MS}ms and "
            f"{MAX_LONG_PRESS_MS // 1000}s"
        )
    return value


def _validate_repeat_time(value):
    value = cv.positive_time_period_milliseconds(value)
    if not MIN_REPEAT_MS <= value.total_milliseconds <= MAX_REPEAT_MS:
        raise cv.Invalid(
            f"repeat_time must be between {MIN_REPEAT_MS}ms and {MAX_REPEAT_MS // 1000}s"
        )
    return value


# Each option on the left needs the option on the right. Long press changes
# when short presses are collected, so it is enabled only by an explicit
# long_press_time, never implicitly by a trigger; repeats follow a long press.
_REQUIRED_OPTIONS = (
    (CONF_ON_KEY_LONG_PRESS, CONF_LONG_PRESS_TIME),
    (CONF_REPEAT_TIME, CONF_LONG_PRESS_TIME),
    (CONF_ON_KEY_REPEAT, CONF_REPEAT_TIME),
)


def _validate_long_press(config: ConfigType) -> ConfigType:
    for option, required in _REQUIRED_OPTIONS:
        if option in config and required not in config:
            raise cv.Invalid(f"{option} requires {required}", path=[option])
    return config


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
            cv.Optional(CONF_LONG_PRESS_TIME): _validate_long_press_time,
            cv.Optional(CONF_ON_KEY_LONG_PRESS): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                        T65KeyLongPressTrigger
                    ),
                }
            ),
            cv.Optional(CONF_REPEAT_TIME): _validate_repeat_time,
            cv.Optional(CONF_ON_KEY_REPEAT): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(T65KeyRepeatTrigger),
                }
            ),
            cv.Optional(CONF_TIMEOUT, default="3s"): _validate_timeout,
            cv.Optional(CONF_ON_NUMBERS): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(T65NumbersTrigger),
                }
            ),
        }
    ),
    _validate_unique_pins,
    _validate_long_press,
    cv.only_on_esp32,
)

automation.register_parented_action(
    "t65_keypad.clear_numbers",
    T65ClearNumbersAction,
    automation.maybe_simple_id(
        {
            cv.GenerateID(): cv.use_id(T65Keypad),
        }
    ),
    synchronous=True,
)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    rows = [await cg.gpio_pin_expression(pin) for pin in config[CONF_ROWS]]
    columns = [await cg.gpio_pin_expression(pin) for pin in config[CONF_COLUMNS]]
    cg.add(var.set_rows(rows))
    cg.add(var.set_columns(columns))
    cg.add(var.set_debounce(config[CONF_DEBOUNCE]))
    cg.add(var.set_number_timeout(config[CONF_TIMEOUT]))
    if CONF_LONG_PRESS_TIME in config:
        cg.add(var.set_long_press_time(config[CONF_LONG_PRESS_TIME]))
    if CONF_REPEAT_TIME in config:
        cg.add(var.set_repeat_time(config[CONF_REPEAT_TIME]))

    for conf in config.get(CONF_ON_KEY, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.register_key_trigger(trigger))
        await automation.build_automation(trigger, [(cg.char, "x")], conf)

    for conf in config.get(CONF_ON_KEY_RELEASE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.register_key_release_trigger(trigger))
        await automation.build_automation(trigger, [(cg.char, "x")], conf)

    for conf in config.get(CONF_ON_KEY_LONG_PRESS, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.register_key_long_press_trigger(trigger))
        await automation.build_automation(trigger, [(cg.char, "x")], conf)

    for conf in config.get(CONF_ON_KEY_REPEAT, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.register_key_repeat_trigger(trigger))
        await automation.build_automation(trigger, [(cg.char, "x")], conf)

    for conf in config.get(CONF_ON_NUMBERS, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.register_numbers_trigger(trigger))
        await automation.build_automation(trigger, [(cg.std_string, "x")], conf)
