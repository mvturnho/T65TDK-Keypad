import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.types import ConfigType

from . import CONF_KEYPAD_ID, T65Keypad

DEPENDENCIES = ["t65_keypad"]

# Same defaults as the rotarydial sensor it replaces, so the Home Assistant
# entity keeps its attributes when a rotary dial is swapped for the keypad.
CONFIG_SCHEMA = sensor.sensor_schema(accuracy_decimals=0).extend(
    {
        cv.GenerateID(CONF_KEYPAD_ID): cv.use_id(T65Keypad),
    }
)


async def to_code(config: ConfigType) -> None:
    var = await sensor.new_sensor(config)
    parent = await cg.get_variable(config[CONF_KEYPAD_ID])
    cg.add_define("USE_T65_KEYPAD_SENSOR")
    cg.add(parent.set_digit_sensor(var))
