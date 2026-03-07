from esphome import codegen as cg
from esphome import config_validation as cv
from esphome.components import sensor
from esphome.components.wmbus_common.driver_loader import (
    FieldType,
    get_human_readable_unit,
)
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


def default_unit_of_measurement(config):
    if CONF_UNIT_OF_MEASUREMENT not in config:
        config[CONF_UNIT_OF_MEASUREMENT] = get_human_readable_unit(config[CONF_FIELD])
    return config


CONFIG_SCHEMA = cv.All(
    BASE_SCHEMA.extend(sensor.sensor_schema(RegularSensor)),
    default_unit_of_measurement,
)

FINAL_VALIDATE_SCHEMA = make_field_validator(FieldType.NUMERIC)


async def to_code(config):
    cg.add_define("USE_WMBUS_METER_SENSOR")
    sensor_ = await sensor.new_sensor(config)
    cg.add(sensor_.set_dynamic_decimals(CONF_ACCURACY_DECIMALS not in config))
    await register_meter(sensor_, config)
