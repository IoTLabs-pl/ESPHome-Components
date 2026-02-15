from esphome import codegen as cg
from esphome.components import text_sensor
from esphome.components.wmbus_common.driver_loader import FieldType

from . import wmbus_meter_ns
from .base_sensor import (
    BASE_SCHEMA,
    BaseSensor,
    make_field_validator,
    register_meter,
)

TextSensor = wmbus_meter_ns.class_("TextSensor", BaseSensor, text_sensor.TextSensor)

CONFIG_SCHEMA = BASE_SCHEMA.extend(text_sensor.text_sensor_schema(TextSensor))

FINAL_VALIDATE_SCHEMA = make_field_validator(FieldType.STRING)


async def to_code(config):
    cg.add_define("USE_WMBUS_METER_TEXT_SENSOR")
    text_sensor_ = await text_sensor.new_text_sensor(config)
    await register_meter(text_sensor_, config)
