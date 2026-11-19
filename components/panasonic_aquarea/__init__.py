from dataclasses import dataclass, field

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID, CONF_UART_ID
from esphome.core import CORE, ID, CoroPriority, coroutine_with_priority

DOMAIN = "panasonic_aquarea"

CODEOWNERS = ["@kubasaw"]

DEPENDENCIES = ["uart"]

CONF_EXTERNAL_CONTROLLER_UART_ID = "external_controller_uart_id"


panasonic_aquarea_ns = cg.esphome_ns.namespace(DOMAIN)
Device = panasonic_aquarea_ns.class_("Device", cg.PollingComponent, uart.UARTDevice)

ReadableEntity = panasonic_aquarea_ns.class_("ReadableEntity")
WritableEntity = panasonic_aquarea_ns.class_(
    "WritableEntity", cg.Parented.template(Device)
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Device),
            cv.Optional(CONF_EXTERNAL_CONTROLLER_UART_ID): cv.use_id(
                uart.UARTComponent
            ),
        }
    )
    .extend(cv.polling_component_schema("3s"))
    .extend(uart.UART_DEVICE_SCHEMA),
    cv.require_esphome_version(2026, 11, 0),
)


def _require_9600_8e1(bus):
    return uart.final_validate_device_schema(
        DOMAIN, uart_bus=bus, baud_rate=9600, data_bits=8, parity="EVEN", stop_bits=1
    )


def _final_validate(config):
    _require_9600_8e1(CONF_UART_ID)(config)
    if CONF_EXTERNAL_CONTROLLER_UART_ID in config:
        _require_9600_8e1(CONF_EXTERNAL_CONTROLLER_UART_ID)(config)


FINAL_VALIDATE_SCHEMA = _final_validate


@dataclass
class ReadableEntities:
    standard: list = field(default_factory=list)
    extra: list = field(default_factory=list)


def readable_entities(hub_id: ID) -> ReadableEntities:
    return CORE.data.setdefault(DOMAIN, {}).setdefault(hub_id.id, ReadableEntities())


@coroutine_with_priority(CoroPriority.FINAL)
async def _set_entities(var, hub_id: ID):
    entities = readable_entities(hub_id)
    if all_entities := entities.standard + entities.extra:
        table = cg.shared_progmem_array(
            f"{DOMAIN}_entities",
            ReadableEntity.operator("ptr"),
            cg.ArrayInitializer(*all_entities, multiline=True),
            constexpr=False,
        )
        cg.add(var.set_entities(table, len(entities.standard)))


async def to_code(config):
    var = await uart.new_uart_component(config)
    await cg.add_variable(
        var.set_external_controller_uart,
        config.get(CONF_EXTERNAL_CONTROLLER_UART_ID),
    )
    CORE.add_job(_set_entities, var, config[CONF_ID])
    return var
