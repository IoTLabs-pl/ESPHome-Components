import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import spi
from esphome.const import CONF_RESET_PIN

from .radio import radio_ns

CONF_RADIO_ID = "radio_id"
CONF_RADIO_TYPE = "radio_type"
CONF_DIO1_PIN = "dio1_pin"


def transceiver_schema(trx_name, schema):
    return (
        cv.Schema(
            {
                cv.GenerateID(CONF_RADIO_ID): cv.declare_id(
                    radio_ns.class_(
                        trx_name,
                        radio_ns.class_("Transceiver"),
                        spi.SPIDevice,
                        cg.Component,
                    )
                ),
                cv.Required(CONF_RESET_PIN): pins.internal_gpio_output_pin_schema,
                cv.Required(CONF_DIO1_PIN): pins.internal_gpio_input_pin_schema,
            }
        )
        .extend(spi.spi_device_schema())
        .extend(cv.COMPONENT_SCHEMA)
        .extend(schema)
    )


async def register_transceiver(config):
    cg.add(cg.LineComment("wM-Bus Transceiver"))

    var = await cg.register_component(cg.new_Pvariable(config[CONF_RADIO_ID]), config)

    reset_pin = await cg.gpio_pin_expression(config[CONF_RESET_PIN])
    cg.add(var.set_reset_pin(reset_pin))

    dio1_pin = await cg.gpio_pin_expression(config[CONF_DIO1_PIN])
    cg.add(var.set_dio1_pin(dio1_pin))

    await spi.register_spi_device(var, config)

    return var
