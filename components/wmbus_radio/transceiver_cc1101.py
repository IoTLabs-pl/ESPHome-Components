import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import spi
from esphome.const import CONF_ID
from .radio import radio_ns
from .transceiver import CONF_RADIO_ID

CONF_GDO0_PIN = "gdo0_pin"
CONF_GDO2_PIN = "gdo2_pin"
CONF_FREQUENCY = "frequency"

CC1101 = radio_ns.class_("CC1101", radio_ns.class_("Transceiver"), spi.SPIDevice, cg.Component)


def config_schema(radio_type, radio_schema):
    return radio_schema.extend(
        cv.Schema({
            cv.GenerateID(CONF_RADIO_ID): cv.declare_id(CC1101),
            cv.Required(CONF_GDO0_PIN): pins.internal_gpio_input_pin_schema,
            cv.Optional(CONF_GDO2_PIN): pins.internal_gpio_input_pin_schema,
            cv.Optional(CONF_FREQUENCY, default="868.95MHz"): cv.All(
                cv.frequency, cv.Range(min=300e6, max=928e6)
            ),
        })
        .extend(spi.spi_device_schema())
        .extend(cv.COMPONENT_SCHEMA)
    )


async def to_code(config):
    var = await cg.register_component(cg.new_Pvariable(config[CONF_RADIO_ID]), config)
    await spi.register_spi_device(var, config)
    pin = await cg.gpio_pin_expression(config[CONF_GDO0_PIN])
    cg.add(var.set_dio1_pin(pin))
    if CONF_GDO2_PIN in config:
        gdo2 = await cg.gpio_pin_expression(config[CONF_GDO2_PIN])
        cg.add(var.set_gdo2_pin(gdo2))
    cg.add(var.set_frequency(config[CONF_FREQUENCY]))
    return var
