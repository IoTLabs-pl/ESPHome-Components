from esphome import config_validation as cv
from esphome.const import CONF_IRQ_PIN

from .transceiver import CONF_DIO1_PIN, register_transceiver, transceiver_schema


def config_schema(radio_type, radio_schema):
    return cv.All(
        cv.rename_key(
            CONF_IRQ_PIN,
            CONF_DIO1_PIN,
            removed_in="2027.3.0",
            component="wmbus_radio",
        ),
        radio_schema.extend(transceiver_schema(radio_type, {})),
    )


to_code = register_transceiver
