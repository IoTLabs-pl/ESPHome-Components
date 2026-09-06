from contextlib import suppress

from esphome import automation
from esphome import codegen as cg
from esphome import config_validation as cv
from esphome.const import CONF_DATA, CONF_FORMAT, CONF_ID, CONF_TRIGGER_ID
from esphome.cpp_generator import LambdaExpression

radio_ns = cg.esphome_ns.namespace("wmbus_radio")
RadioComponent = radio_ns.class_("Radio", cg.Component)

CONF_MARK_AS_HANDLED = "mark_as_handled"
CONF_ON_FRAME = "on_frame"
CONF_ON_PACKET = "on_packet"
CONF_PACKET_TRIGGER_ID = "packet_trigger_id"

FramePtr = radio_ns.class_("Frame").operator("ptr")
FrameTrigger = radio_ns.class_("FrameTrigger", automation.Trigger.template(FramePtr))
PacketPtr = radio_ns.class_("Packet").operator("ptr")
PacketTrigger = radio_ns.class_("PacketTrigger", automation.Trigger.template(PacketPtr))


SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RadioComponent),
        cv.Optional(CONF_ON_FRAME): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(FrameTrigger),
                cv.Optional(CONF_MARK_AS_HANDLED, default=False): cv.boolean,
            }
        ),
        cv.Optional(CONF_ON_PACKET): automation.validate_automation(
            {
                cv.GenerateID(CONF_PACKET_TRIGGER_ID): cv.declare_id(PacketTrigger),
            }
        ),
    }
)


async def register_radio(config):
    cg.add(cg.LineComment("wM-Bus Component"))

    var = await cg.register_component(cg.new_Pvariable(config[CONF_ID]), config)

    for conf in config.get(CONF_ON_FRAME, []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var, conf[CONF_MARK_AS_HANDLED])
        await automation.build_automation(
            trig,
            [(FramePtr, "frame")],
            conf,
        )

    for conf in config.get(CONF_ON_PACKET, []):
        trig = cg.new_Pvariable(conf[CONF_PACKET_TRIGGER_ID], var)
        await automation.build_automation(
            trig,
            [(PacketPtr, "packet")],
            conf,
        )

    return var


with suppress(ImportError):
    from esphome.components.socket_transmitter import (
        SOCKET_SEND_ACTION_SCHEMA,
        SocketTransmitterSendAction,
    )

    OUTPUT_FORMATS = {
        "hex": cg.std_string,
        "raw": cg.std_vector.template(cg.uint8),
        "rtlwmbus": cg.std_string,
    }

    FRAME_SOCKET_SEND_SCHEMA = SOCKET_SEND_ACTION_SCHEMA.extend(
        {
            cv.Required(CONF_FORMAT): cv.one_of(
                *OUTPUT_FORMATS.keys(),
                lower=True,
            ),
            cv.Optional(CONF_DATA): cv.invalid(
                "If you want to specify data to be sent, use generic 'socket_transmitter.send' action"
            ),
        }
    )

    @automation.register_action(
        "wmbus_radio.send_frame_with_socket",
        SocketTransmitterSendAction,
        FRAME_SOCKET_SEND_SCHEMA,
        synchronous=True,
    )
    async def send_frame_with_socket_to_code(config, action_id, template_arg, args):
        output_type = OUTPUT_FORMATS[config[CONF_FORMAT]]

        paren = await cg.get_variable(config[CONF_ID])
        var = cg.new_Pvariable(
            action_id, cg.TemplateArguments(output_type, *template_arg), paren
        )
        template_ = LambdaExpression(
            f"return frame->as_{config[CONF_FORMAT]}();", args, ""
        )

        cg.add(var.set_data(template_))

        return var
