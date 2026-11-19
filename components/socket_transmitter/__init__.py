from esphome import config_validation as cv
from esphome import codegen as cg
from esphome import automation
from esphome.components import network, socket
from esphome.const import CONF_ID, CONF_IP_ADDRESS, CONF_PORT, CONF_PROTOCOL, CONF_DATA

DOMAIN = "socket_transmitter"

AUTO_LOAD = ["socket"]

DEPENDENCIES = ["network"]

MULTI_CONF = True

CODEOWNERS = ["@kubasaw"]


socket_ns = cg.esphome_ns.namespace(DOMAIN)
SocketTransmitter = socket_ns.class_("SocketTransmitter", cg.Component)


def _consume_socket(config):
    socket_type = socket.SocketType[config[CONF_PROTOCOL]]
    return socket.consume_sockets(1, DOMAIN, socket_type)(config)


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SocketTransmitter),
            cv.Required(CONF_IP_ADDRESS): cv.All(cv.ipv4address, cv.string),
            cv.Required(CONF_PORT): cv.port,
            cv.Required(CONF_PROTOCOL): cv.enum(
                {
                    "TCP": cg.RawExpression("SOCK_STREAM"),
                    "UDP": cg.RawExpression("SOCK_DGRAM"),
                },
                upper=True,
            ),
        }
    ),
    cv.require_esphome_version(2026, 11, 0),
    _consume_socket,
    network.require_ipv4(DOMAIN),
)


async def to_code(config):
    var = cg.new_Pvariable(
        config[CONF_ID],
        config[CONF_IP_ADDRESS],
        config[CONF_PORT],
        config[CONF_PROTOCOL],
    )
    await cg.register_component(var, config)


SOCKET_SEND_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(SocketTransmitter),
        cv.Required(CONF_DATA): cv.templatable(cv.string_strict),
    }
)

automation.register_apply_action(
    "socket_transmitter.send",
    SOCKET_SEND_ACTION_SCHEMA,
    automation.ApplyField(CONF_DATA, "send", cg.std_string),
)
