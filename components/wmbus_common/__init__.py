from pathlib import Path

import esphome.config_validation as cv
from esphome import codegen as cg
from esphome.const import CONF_ID, SOURCE_FILE_EXTENSIONS
from esphome.core import CORE
from esphome.helpers import write_file_if_changed

from .drivers.xmq_to_cc import generate

CODEOWNERS = ["@kubasaw"]
CONF_DRIVERS = "drivers"

wmbus_common_ns = cg.esphome_ns.namespace("wmbus_common")
WMBusCommon = wmbus_common_ns.class_("WMBusCommon", cg.Component)

DRIVER_CC_PREFIX = "driver_"

CURRENT_DIR = Path(__file__).parent
XMQ_DRIVERS_SRC_PATH = CURRENT_DIR / "drivers" / "src"

SOURCE_FILE_EXTENSIONS.add(".cc")

ALWAYS_EXCLUDED_LEGACY_DRIVERS = {"auto", "unknown"}

_registered_drivers: set[str] = set()


def discover_legacy_drivers() -> set[str]:
    return {
        f.stem.removeprefix(DRIVER_CC_PREFIX)
        for f in CURRENT_DIR.glob(f"{DRIVER_CC_PREFIX}*.cc")
        if f.stem.removeprefix(DRIVER_CC_PREFIX) not in ALWAYS_EXCLUDED_LEGACY_DRIVERS
    }


def discover_xmq_drivers() -> set[str]:
    return {f.stem for f in XMQ_DRIVERS_SRC_PATH.glob("*.xmq")}


LEGACY_DRIVERS = discover_legacy_drivers()
XMQ_DRIVERS = discover_xmq_drivers()
AVAILABLE_DRIVERS = LEGACY_DRIVERS | XMQ_DRIVERS


def _validate_and_register_driver(driver: str) -> str:
    _registered_drivers.add(driver)
    return driver


validate_driver = cv.All(
    cv.one_of(*sorted(AVAILABLE_DRIVERS), lower=True, space="_"),
    _validate_and_register_driver,
)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WMBusCommon),
        cv.Optional(CONF_DRIVERS): cv.All(
            lambda x: list(AVAILABLE_DRIVERS) if x == "all" else x,
            [validate_driver],
        ),
    }
)


def current_generated_drivers(path: Path) -> set[str]:
    if not path.exists():
        return set()

    return {
        p.stem.removeprefix(DRIVER_CC_PREFIX)
        for p in path.glob(f"{DRIVER_CC_PREFIX}*.cc")
    }


def generate_drivers(drivers: set[str], target_dir: Path):
    for driver in drivers:
        content = generate(XMQ_DRIVERS_SRC_PATH / f"{driver}.xmq")
        write_file_if_changed(
            target_dir / f"{DRIVER_CC_PREFIX}{driver}.cc",
            content,
        )


def cleanup_drivers(drivers: set[str], target_dir: Path):
    for driver in drivers:
        (target_dir / f"{DRIVER_CC_PREFIX}{driver}.cc").unlink(missing_ok=True)


def remove_directory(path: Path):
    if not path.exists():
        return

    for f in path.glob("*"):
        f.unlink(missing_ok=True)

    path.rmdir()


async def to_code(config):
    cg.add_define(
        "WMBUSMETERS_TAG",
        CURRENT_DIR.joinpath(".wmbusmeters_tag").read_text(),
    )

    active_xmq_drivers = XMQ_DRIVERS & _registered_drivers
    target_dir = CORE.relative_src_path("xmq_drivers")

    if active_xmq_drivers:
        target_dir.mkdir(exist_ok=True, parents=True)

        existing_xmq_drivers = current_generated_drivers(target_dir)

        generate_drivers(active_xmq_drivers, target_dir)
        cleanup_drivers(existing_xmq_drivers - active_xmq_drivers, target_dir)
    else:
        remove_directory(target_dir)

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)


def FILTER_SOURCE_FILES() -> list[str]:
    excluded = LEGACY_DRIVERS - _registered_drivers | ALWAYS_EXCLUDED_LEGACY_DRIVERS
    return [f"{DRIVER_CC_PREFIX}{name}.cc" for name in excluded]
