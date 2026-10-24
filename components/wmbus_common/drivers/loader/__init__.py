import esphome.config_validation as cv

from . import driver
from .driver import GENERATED_DIR, AutoDriver, Driver  # noqa: F401
from .units import UNITS, Unit, split_unit  # noqa: F401
from .xmq_loader import FieldKind  # noqa: F401

DriverManager = driver.DriverManager()

AUTO = "auto"

validate_driver_name = cv.All(cv.one_of(*DriverManager.available_drivers), DriverManager.request_driver)


def validate_meter_type(value):
    if value == AUTO:
        return DriverManager.request_auto_driver()

    return validate_driver_name(value)
