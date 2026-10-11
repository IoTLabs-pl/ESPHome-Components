"""Test-only; lives under tests/ so it never reaches users."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@kubasaw"]
DEPENDENCIES = ["wmbus_common"]

wmbus_test_runner_ns = cg.esphome_ns.namespace("wmbus_test_runner")
TestRunner = wmbus_test_runner_ns.class_("TestRunner", cg.Component)

CONFIG_SCHEMA = cv.Schema({cv.GenerateID(): cv.declare_id(TestRunner)})


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
