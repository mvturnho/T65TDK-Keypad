import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import event
from esphome.core import CORE
from esphome.types import ConfigType

from . import CONF_KEYPAD_ID, CONF_LONG_PRESS_TIME, CONF_REPEAT_TIME, T65Keypad

DEPENDENCIES = ["t65_keypad"]

EVENT_TYPES = [
    "key_0",
    "key_1",
    "key_2",
    "key_3",
    "key_4",
    "key_5",
    "key_6",
    "key_7",
    "key_8",
    "key_9",
    "key_star",
    "key_hash",
]
# Advertised only when the keypad has long_press_time and repeat_time respectively.
LONG_EVENT_TYPES = [f"{event_type}_long" for event_type in EVENT_TYPES]
REPEAT_EVENT_TYPES = [f"{event_type}_repeat" for event_type in EVENT_TYPES]

CONFIG_SCHEMA = event.event_schema(device_class="button").extend(
    {
        cv.GenerateID(CONF_KEYPAD_ID): cv.use_id(T65Keypad),
    }
)


def _event_types() -> list[str]:
    # t65_keypad is a single-instance component, so its config is one mapping.
    keypad = CORE.config.get("t65_keypad", {})
    event_types = list(EVENT_TYPES)
    if CONF_LONG_PRESS_TIME in keypad:
        event_types += LONG_EVENT_TYPES
    if CONF_REPEAT_TIME in keypad:
        event_types += REPEAT_EVENT_TYPES
    return event_types


async def to_code(config: ConfigType) -> None:
    event_types = _event_types()
    var = await event.new_event(config, event_types=event_types)
    parent = await cg.get_variable(config[CONF_KEYPAD_ID])
    cg.add_define("USE_T65_KEYPAD_EVENT")
    cg.add(parent.set_event(var))
