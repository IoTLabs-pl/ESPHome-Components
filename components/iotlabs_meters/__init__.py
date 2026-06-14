import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import http_request
from esphome.components.wmbus_radio import RadioComponent
from esphome.const import CONF_ID

AUTO_LOAD = ["http_request"]
DEPENDENCIES = ["wmbus_radio"]

iotlabs_ns = cg.esphome_ns.namespace("iotlabs_meters")
IoTLabsMetersClient = iotlabs_ns.class_("IoTLabsMetersClient", cg.Component)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(IoTLabsMetersClient),
        cv.GenerateID("http_id"): cv.use_id(http_request.HttpRequestComponent),
        cv.GenerateID("radio_id"): cv.use_id(RadioComponent),
        cv.Optional("base_url", default="https://meters.iotlabs.pl/api/ingest"): cv.url,
        cv.Required("api_key"): cv.string_strict,
        cv.Optional("max_delivery_attempts", default=3): cv.int_range(min=1, max=10),
        cv.Optional(
            "delivery_retry_interval", default="1s"
        ): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add_define("USE_HTTP_REQUEST_RESPONSE")

    cg.add(var.set_http_request(await cg.get_variable(config["http_id"])))
    cg.add(var.set_wmbus_radio(await cg.get_variable(config["radio_id"])))
    cg.add(var.set_base_url(config["base_url"]))
    cg.add(var.set_auth_header(f"Bearer {config['api_key']}"))
    cg.add(
        var.set_delivery_retry_interval_ms(
            config["delivery_retry_interval"].total_milliseconds
        )
    )
    cg.add(var.set_max_delivery_attempts(config["max_delivery_attempts"]))
