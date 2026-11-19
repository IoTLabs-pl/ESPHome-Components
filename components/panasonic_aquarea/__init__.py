import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_UART_ID

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


async def to_code(config):
    var = await uart.new_uart_component(config)
    await cg.add_variable(
        var.set_external_controller_uart,
        config.get(CONF_EXTERNAL_CONTROLLER_UART_ID),
    )
    return var
