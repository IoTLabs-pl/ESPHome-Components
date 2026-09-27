from esphome import codegen as cg
from esphome import config_validation as cv
from esphome.components import sensor
from esphome.components.wmbus_common.drivers.loader import UNITS, FieldKind, Unit, split_unit
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

# The resolution meters commonly send each unit in; any other unit is a whole number.
DEFAULT_ACCURACY_DECIMALS = {
    Unit.M3: 3,
    Unit.M3H: 3,
    Unit.KWH: 3,
    Unit.KVARH: 3,
    Unit.KVAH: 3,
    Unit.KW: 3,
    Unit.KVAR: 3,
    Unit.KVA: 3,
    Unit.Ampere: 3,
    Unit.FACTOR: 3,
    Unit.C: 2,
    Unit.Volt: 2,
    Unit.Hertz: 2,
    Unit.BAR: 2,
    Unit.RH: 1,
    Unit.DEGREE: 1,
    Unit.Year: 1,
}


def unit_defaults(config):
    _, unit = split_unit(config[CONF_FIELD])
    config.setdefault(CONF_UNIT_OF_MEASUREMENT, UNITS[unit].display if unit else "?")
    config.setdefault(CONF_ACCURACY_DECIMALS, DEFAULT_ACCURACY_DECIMALS.get(unit, 0))
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
