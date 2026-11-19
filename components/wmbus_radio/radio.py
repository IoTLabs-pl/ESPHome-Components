from contextlib import suppress

from esphome import automation
from esphome import codegen as cg
from esphome import config_validation as cv
from esphome.components.const import CONF_ON_PACKET
from esphome.const import CONF_DATA, CONF_FORMAT, CONF_ID

DOMAIN = "wmbus_radio"

radio_ns = cg.esphome_ns.namespace(DOMAIN)
RadioComponent = radio_ns.class_("Radio", cg.Component)

CONF_MARK_AS_HANDLED = "mark_as_handled"
CONF_ON_FRAME = "on_frame"

FramePtr = radio_ns.class_("Frame").operator("ptr")
PacketPtr = radio_ns.class_("Packet").operator("ptr")
MarkHandledForwarder = radio_ns.struct("MarkHandledForwarder")


SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RadioComponent),
        cv.Optional(CONF_ON_FRAME): automation.validate_automation(
            {
                cv.Optional(CONF_MARK_AS_HANDLED, default=False): cv.boolean,
            }
        ),
        cv.Optional(CONF_ON_PACKET): automation.validate_automation({}),
    }
)


async def register_radio(config):
    cg.add(cg.LineComment("wM-Bus Component"))

    var = await cg.register_component(cg.new_Pvariable(config[CONF_ID]), config)

    for conf in config.get(CONF_ON_FRAME, []):
        await automation.build_callback_automation(
            var,
            "on_frame",
            [(FramePtr, "frame")],
            conf,
            forwarder=MarkHandledForwarder if conf[CONF_MARK_AS_HANDLED] else None,
        )

    for conf in config.get(CONF_ON_PACKET, []):
        await automation.build_callback_automation(
            var, "on_packet", [(PacketPtr, "packet")], conf
        )

    return var


with suppress(ImportError):
    from esphome.components.socket_transmitter import SOCKET_SEND_ACTION_SCHEMA

    FRAME_SOCKET_SEND_SCHEMA = SOCKET_SEND_ACTION_SCHEMA.extend(
        {
            cv.Required(CONF_FORMAT): cv.one_of("hex", "raw", "rtlwmbus", lower=True),
            cv.Optional(CONF_DATA): cv.invalid(
                "If you want to specify data to be sent, use generic 'socket_transmitter.send' action"
            ),
        }
    )

    @automation.register_action(
        "wmbus_radio.send_frame_with_socket",
        automation.ApplyAction,
        FRAME_SOCKET_SEND_SCHEMA,
        synchronous=True,
    )
    async def send_frame_with_socket_to_code(config, action_id, template_arg, args):
        parent = await cg.get_variable(config[CONF_ID])
        return automation.apply_statements_action(
            action_id,
            template_arg,
            args,
            [f"{parent}->send(frame->as_{config[CONF_FORMAT]}());"],
        )
