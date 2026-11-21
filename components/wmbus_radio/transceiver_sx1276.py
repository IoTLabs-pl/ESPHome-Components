import esphome.codegen as cg
from esphome import config_validation as cv
from esphome import pins
from esphome.const import CONF_IRQ_PIN, CONF_RESET_PIN

from .transceiver import register_transceiver, transceiver_schema

CONF_DIO1_PIN = "dio1_pin"
CONF_LNA_BOOST = "lna_boost"


def config_schema(radio_type, radio_schema):
    return cv.All(
        cv.rename_key(
            CONF_IRQ_PIN,
            CONF_DIO1_PIN,
            removed_in="2027.3.0",
            component="wmbus_radio",
        ),
        radio_schema.extend(
            transceiver_schema(
                radio_type,
                {
                    cv.Required(CONF_RESET_PIN): pins.internal_gpio_output_pin_schema,
                    cv.Required(CONF_DIO1_PIN): pins.internal_gpio_input_pin_schema,
                    cv.Optional(CONF_LNA_BOOST, default=False): cv.boolean,
                },
            )
        ),
    )


async def to_code(config):
    var = await register_transceiver(config)

    await cg.add_pin(var.set_reset_pin, config[CONF_RESET_PIN])
    await cg.add_pin(var.set_dio1_pin, config[CONF_DIO1_PIN])
    cg.add(var.set_lna_boost(config[CONF_LNA_BOOST]))

    return var
