import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components.mqtt import MQTT_PUBLISH_ACTION_SCHEMA
from esphome.components.wmbus_common.drivers.loader import (
    AutoDriver,
    DriverManager,
    validate_meter_type,
)
from esphome.components.wmbus_radio import RadioComponent
from esphome.const import (
    CONF_ID,
    CONF_KEY,
    CONF_PAYLOAD,
    CONF_TYPE,
)
from esphome.core import HexInt

CONF_METER_ID = "meter_id"
CONF_RADIO_ID = "radio_id"
CONF_ON_TELEGRAM = "on_telegram"

DOMAIN = "wmbus_meter"

CODEOWNERS = ["@kubasaw"]

DEPENDENCIES = ["wmbus_radio"]
AUTO_LOAD = ["sensor", "text_sensor"]

MULTI_CONF = True


wmbus_meter_ns = cg.esphome_ns.namespace(DOMAIN)
Meter = wmbus_meter_ns.class_("Meter", cg.Component)
MeterPtr = Meter.operator("ptr")


def meter_id_validator(meter_id):
    meter_id = cv.string(meter_id).lower().removeprefix("0x")
    meter_id = cv.Length(1, 8)(meter_id)

    try:
        value = int(meter_id, 16)
    except ValueError:
        raise cv.Invalid("Meter ID must be a valid hexadecimal string")
    if value < 0:
        raise cv.Invalid("Meter ID must be a positive hexadecimal integer")

    return HexInt(value)


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Meter),
            cv.GenerateID(CONF_RADIO_ID): cv.use_id(RadioComponent),
            cv.Required(CONF_METER_ID): meter_id_validator,
            cv.Required(CONF_TYPE): validate_meter_type,
            cv.Optional(CONF_KEY): cv.bind_key(name="Key"),
            cv.Optional(CONF_ON_TELEGRAM): automation.validate_automation({}),
        },
    ).extend(cv.COMPONENT_SCHEMA),
    cv.require_esphome_version(2026, 11, 0),
)


async def to_code(config):
    meter = cg.new_Pvariable(config[CONF_ID])
    driver = config[CONF_TYPE]

    # `auto` detects the driver from the first telegram.
    if isinstance(driver, AutoDriver):
        spec = cg.RawExpression("nullptr")
    else:
        index = DriverManager.registry_index(driver)
        spec = cg.RawExpression(f"wmbus::registered_drivers[{index}] /* {driver.name} */")

    key = config.get(CONF_KEY)
    key_bytes = cg.ArrayInitializer(*map(HexInt, bytes.fromhex(key))) if key else None
    cg.add(
        meter.set_driver(spec, config[CONF_METER_ID], cg.ArrayInitializer(key_bytes))
    )

    await cg.add_variable(meter.set_radio, config[CONF_RADIO_ID])
    await cg.register_component(meter, config)

    for conf in config.get(CONF_ON_TELEGRAM, []):
        await automation.build_parent_callback_automation(
            meter, "on_telegram", (MeterPtr, "meter"), conf
        )


TELEGRAM_MQTT_PUBLISH_ACTION_SCHEMA = cv.All(
    MQTT_PUBLISH_ACTION_SCHEMA.extend(
        {
            cv.Optional(CONF_PAYLOAD): cv.invalid(
                "If you want to specify payload, use generic 'mqtt.publish' action"
            )
        }
    ),
    lambda c: {**c, CONF_PAYLOAD: cv.Lambda("return meter->as_json();")},
)


_MQTT_PUBLISH = automation.ACTION_REGISTRY["mqtt.publish"]

automation.register_action(
    "wmbus_meter.send_telegram_with_mqtt",
    _MQTT_PUBLISH.type_id,
    TELEGRAM_MQTT_PUBLISH_ACTION_SCHEMA,
    synchronous=True,
)(_MQTT_PUBLISH.fun)
