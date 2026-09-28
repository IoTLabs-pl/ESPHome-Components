import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_BUSY_PIN

from .radio import radio_ns
from .transceiver import register_transceiver, transceiver_schema

CONF_TCXO_VOLTAGE = "tcxo_voltage"
CONF_RF_SWITCH = "rf_switch"
CONF_USE_DCDC = "use_dcdc"
CONF_RX_BOOST = "rx_boost"

TcxoVoltage = radio_ns.class_("SX1262").enum("TcxoVoltage", is_class=True)

TCXO_VOLTAGE = {
    "0mV": TcxoVoltage.NONE,
    "1600mV": TcxoVoltage.V1_6,
    "1700mV": TcxoVoltage.V1_7,
    "1800mV": TcxoVoltage.V1_8,
    "2200mV": TcxoVoltage.V2_2,
    "2400mV": TcxoVoltage.V2_4,
    "2700mV": TcxoVoltage.V2_7,
    "3000mV": TcxoVoltage.V3_0,
    "3300mV": TcxoVoltage.V3_3,
}


def config_schema(radio_type, radio_schema):
    return radio_schema.extend(
        transceiver_schema(
            radio_type,
            {
                cv.Required(CONF_BUSY_PIN): pins.internal_gpio_input_pin_schema,
                cv.Optional(CONF_TCXO_VOLTAGE, default="0V"): cv.All(
                    cv.voltage,
                    lambda volts: f"{round(volts * 1000)}mV",
                    cv.enum(TCXO_VOLTAGE),
                ),
                cv.Optional(CONF_RF_SWITCH, default=False): cv.boolean,
                cv.Optional(CONF_USE_DCDC, default=True): cv.boolean,
                cv.Optional(CONF_RX_BOOST, default=False): cv.boolean,
            },
        )
    )


async def to_code(config):
    var = await register_transceiver(config)

    busy_pin = await cg.gpio_pin_expression(config[CONF_BUSY_PIN])
    cg.add(var.set_busy_pin(busy_pin))
    cg.add(var.set_tcxo_voltage(config[CONF_TCXO_VOLTAGE]))
    cg.add(var.set_rf_switch(config[CONF_RF_SWITCH]))
    cg.add(var.set_use_dcdc(config[CONF_USE_DCDC]))
    cg.add(var.set_rx_boost(config[CONF_RX_BOOST]))

    return var
