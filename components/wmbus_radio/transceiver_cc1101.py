"""CC1101 radio type for IoTLabs wmbus_radio (ESPHome 2026)."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins

from .transceiver import register_transceiver, transceiver_schema

CONF_GDO0_PIN = "gdo0_pin"
CONF_GDO2_PIN = "gdo2_pin"
CONF_FREQUENCY = "frequency"


def config_schema(radio_type, radio_schema):
    return radio_schema.extend(
        transceiver_schema(
            radio_type,
            {
                cv.Required(CONF_GDO0_PIN): pins.internal_gpio_input_pin_schema,
                cv.Required(CONF_GDO2_PIN): pins.internal_gpio_input_pin_schema,
                cv.Optional(CONF_FREQUENCY, default="868.95MHz"): cv.All(
                    cv.frequency, cv.Range(min=300e6, max=928e6)
                ),
            },
        )
    )


async def to_code(config):
    var = await register_transceiver(config)
    gdo2 = await cg.gpio_pin_expression(config[CONF_GDO2_PIN])
    cg.add(var.set_gdo2_pin(gdo2))
    gdo0 = await cg.gpio_pin_expression(config[CONF_GDO0_PIN])
    cg.add(var.set_gdo0_pin(gdo0))
    cg.add(var.set_frequency(config[CONF_FREQUENCY]))
    return var
