import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = ["@kubasaw"]

CONF_EXTERNAL_CONTROLLER_UART_ID = "external_controller_uart_id"


panasonic_aquarea_ns = cg.esphome_ns.namespace("panasonic_aquarea")
Device = panasonic_aquarea_ns.class_("Device", cg.PollingComponent, uart.UARTDevice)

ReadableEntity = panasonic_aquarea_ns.class_("ReadableEntity")
WritableEntity = panasonic_aquarea_ns.class_(
    "WritableEntity", cg.Parented.template(Device)
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Device),
            cv.Optional(CONF_EXTERNAL_CONTROLLER_UART_ID): cv.use_id(
                uart.UARTComponent
            ),
        }
    )
    .extend(cv.polling_component_schema("3s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_EXTERNAL_CONTROLLER_UART_ID in config:
        uart_client = await cg.get_variable(config[CONF_EXTERNAL_CONTROLLER_UART_ID])
        cg.add(var.set_external_controller_uart(uart_client))

    return var
