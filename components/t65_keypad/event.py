import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import event
from esphome.types import ConfigType

from . import T65Keypad

DEPENDENCIES = ["t65_keypad"]

CONF_KEYPAD_ID = "keypad_id"

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

CONFIG_SCHEMA = event.event_schema(device_class="button").extend(
    {
        cv.GenerateID(CONF_KEYPAD_ID): cv.use_id(T65Keypad),
    }
)


async def to_code(config: ConfigType) -> None:
    var = await event.new_event(config, event_types=EVENT_TYPES)
    parent = await cg.get_variable(config[CONF_KEYPAD_ID])
    cg.add_define("USE_T65_KEYPAD_EVENT")
    cg.add(parent.set_event(var))
