import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import spi

from .radio import radio_ns

CONF_RADIO_ID = "radio_id"
CONF_RADIO_TYPE = "radio_type"


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
            }
        )
        .extend(spi.spi_device_schema())
        .extend(cv.COMPONENT_SCHEMA)
        .extend(schema)
    )


async def register_transceiver(config):
    cg.add(cg.LineComment("wM-Bus Transceiver"))

    var = await cg.register_component(cg.new_Pvariable(config[CONF_RADIO_ID]), config)

    await spi.register_spi_device(var, config)

    return var
