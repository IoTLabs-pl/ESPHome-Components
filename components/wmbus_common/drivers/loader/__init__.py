import esphome.config_validation as cv

from .driver import AutoDriver, Driver
from .driver_manager import GENERATED_DIR
from .driver_manager import DriverManager as _DriverManagerClass
from .units import UNITS, Unit, split_unit
from .xmq_loader import FieldKind

DriverManager = _DriverManagerClass()
DriverManager.load_drivers()

AUTO = "auto"

validate_driver_name = cv.All(cv.one_of(*DriverManager.available_drivers), DriverManager.request_driver)


def validate_meter_type(value):
    if value == AUTO:
        return DriverManager.request_auto_driver()

    return validate_driver_name(value)


__all__ = [
    "AUTO",
    "AutoDriver",
    "DriverManager",
    "GENERATED_DIR",
    "UNITS",
    "Unit",
    "validate_driver_name",
    "validate_meter_type",
    "Driver",
    "FieldKind",
    "split_unit",
]
