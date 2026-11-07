from esphome import codegen as cg
from esphome import config_validation as cv
from esphome.components import sensor
from esphome.components.wmbus_common.drivers.loader import UNITS, FieldKind, split_unit
from esphome.components.wmbus_common.drivers.loader.metadata import DEFAULT_ACCURACY_DECIMALS
from esphome.const import CONF_ACCURACY_DECIMALS, CONF_UNIT_OF_MEASUREMENT

from . import wmbus_meter_ns
from .base_sensor import (
    BASE_SCHEMA,
    CONF_FIELD,
    BaseSensor,
    make_field_validator,
    register_meter,
)

RegularSensor = wmbus_meter_ns.class_("Sensor", BaseSensor, sensor.Sensor)

def unit_defaults(config):
    _, unit = split_unit(config[CONF_FIELD])
    if unit:
        config.setdefault(CONF_UNIT_OF_MEASUREMENT, UNITS[unit].display)
        config.setdefault(CONF_ACCURACY_DECIMALS, DEFAULT_ACCURACY_DECIMALS.get(unit, 0))
    else:
        config.setdefault(CONF_ACCURACY_DECIMALS, 0)
    return config


CONFIG_SCHEMA = cv.All(
    BASE_SCHEMA.extend(sensor.sensor_schema(RegularSensor)),
    unit_defaults,
)

FINAL_VALIDATE_SCHEMA = make_field_validator(FieldKind.Numeric)


async def to_code(config):
    cg.add_define("USE_WMBUS_METER_SENSOR")
    sensor_ = await sensor.new_sensor(config)
    await register_meter(sensor_, config)
