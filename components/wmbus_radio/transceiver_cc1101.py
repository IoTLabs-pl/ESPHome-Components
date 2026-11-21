import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins

from .radio import radio_ns
from .transceiver import register_transceiver, transceiver_schema

CONF_GDO0_PIN = "gdo0_pin"
CONF_GDO2_PIN = "gdo2_pin"

Register = radio_ns.class_("CC1101").enum("Register", is_class=True)


def config_schema(radio_type, radio_schema):
    return cv.All(
        radio_schema.extend(
            transceiver_schema(
                radio_type,
                {
                    cv.Optional(CONF_GDO0_PIN): pins.internal_gpio_input_pin_schema,
                    cv.Optional(CONF_GDO2_PIN): pins.internal_gpio_input_pin_schema,
                },
            )
        ),
        cv.has_exactly_one_key(CONF_GDO0_PIN, CONF_GDO2_PIN),
    )


async def to_code(config):
    var = await register_transceiver(config)

    await cg.add_pin(lambda pin: var.set_gdo_pin(pin, Register.IOCFG0), config.get(CONF_GDO0_PIN))
    await cg.add_pin(lambda pin: var.set_gdo_pin(pin, Register.IOCFG2), config.get(CONF_GDO2_PIN))

    return var
