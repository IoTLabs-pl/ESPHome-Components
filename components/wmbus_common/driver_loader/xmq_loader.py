from pathlib import Path

from esphome import codegen as cg
from esphome.config_validation import ensure_list, hex_int
from esphome.cpp_generator import ExpressionStatement
from jinja2 import Template
from voluptuous import All, Any, Coerce, Optional, Remove, Required, Schema


def parse_xmq(content: str):
    result = {}
    stack = [result]
    in_block_comment = False

    def add_to_parent(key, value):
        parent = stack[-1]
        if isinstance(parent, list):
            parent.append(value)
        elif key in parent:
            if not isinstance(parent[key], list):
                parent[key] = [parent[key]]
            parent[key].append(value)
        else:
            parent[key] = value

    for raw_line in content.split("\n"):
        line = raw_line.strip()
        if "//" in line:
            line = line.split("//", 1)[0].strip()
        if not line:
            continue
        if in_block_comment:
            if "*/" in line:
                in_block_comment = False
            continue
        if line.startswith("/*"):
            if "*/" not in line:
                in_block_comment = True
            continue

        if "=" in line:
            key, val = (p.strip().strip("'\"") for p in line.split("=", 1))
            add_to_parent(key, val)
        elif "{" in line:
            key = line.split("{")[0].strip()
            new_node = {}
            add_to_parent(key, new_node)
            stack.append(new_node)
        elif "}" in line:
            if len(stack) > 1:
                stack.pop()

    return result


ns = cg.global_ns
Translate = ns.namespace("Translate")
Quantity = ns.namespace("Quantity")
FieldMatcher = ns.namespace("FieldMatcher")
Unit = ns.namespace("Unit")

CSV_LIST = All(str, lambda x: [s.strip() for s in x.split(",")])


def frac(s: str):
    num, _, denom = s.partition("/")
    return float(num) / float(denom)


MATCHER_SCHEMA = {
    Optional("measurement_type"): str,
    Optional("difvifkey"): str,
    Optional("vif_range"): str,
    Optional("vif_scaling"): str,
    Optional("tariff_nr"): All(CSV_LIST, [Coerce(int)]),
    Optional("subunit_nr"): All(CSV_LIST, [Coerce(int)]),
    Optional("storage_nr"): All(CSV_LIST, [Coerce(int)]),
    Optional("index_nr"): Coerce(int),
    Optional("add_combinable"): str,
}
LOOKUP_SCHEMA = Schema(
    {
        Required("name"): str,
        Required("map_type"): str,
        Required("mask_bits"): hex_int,
        Optional("default_message", default=""): str,
        Optional("map", default=[]): ensure_list(
            {
                Required("name"): str,
                Required("value"): hex_int,
                Optional("info"): str,
                Required("test"): str,
            }
        ),
    }
)

NUMERIC_FIELD_WITH_EXTRACTOR = Schema(
    {
        Required("name"): str,
        Optional("info"): str,
        Optional("attributes", default=""): str,
        Required("quantity"): str,
        Optional("vif_scaling", default=""): str,
        Optional("dif_signedness", default=""): str,
        Required("match"): MATCHER_SCHEMA,
        Optional("display_unit", default=""): str,
        Optional("force_scale", default=1.0): Any(Coerce(float), frac),
    }
)

NUMERIC_FIELD_WITH_CALCULATOR = Schema(
    {
        Required("name"): str,
        Optional("info"): str,
        Optional("attributes", default=""): str,
        Required("quantity"): str,
        Required("calculate"): str,
        Optional("display_unit", default=""): str,
    }
)

NUMERIC_FIELD_WITH_CALCULATOR_AND_MATCHER = Schema(
    {
        Required("name"): str,
        Optional("info"): str,
        Optional("attributes", default=""): str,
        Required("quantity"): str,
        Required("formula"): str,
        Required("match"): MATCHER_SCHEMA,
        Optional("display_unit", default=""): str,
    }
)

NUMERIC_FIELD = Schema(
    {
        Required("name"): str,
        Required("quantity"): str,
        Optional("attributes", default=""): str,
        Optional("info"): str,
        Optional("display_unit", default=""): str,
    }
)

STRING_FIELD_WITH_EXTRACTOR = Schema(
    {
        Required("name"): str,
        Required("quantity"): "Text",
        Optional("info"): str,
        Optional("attributes", default=""): str,
        Required("match"): MATCHER_SCHEMA,
    }
)

STRING_FIELD_WITH_EXTRACTOR_AND_LOOKUP = Schema(
    {
        Required("name"): str,
        Required("quantity"): "Text",
        Optional("info"): str,
        Optional("attributes", default=""): str,
        Required("match"): MATCHER_SCHEMA,
        Required("lookup"): LOOKUP_SCHEMA,
    }
)

STRING_FIELD = Schema(
    {
        Required("name"): str,
        Required("quantity"): "Text",
        Optional("info"): str,
        Optional("attributes", default=""): str,
    }
)


def parse_mvt(v: list[str]):
    m, v, t = v
    if len(m) == 3 and m.isalpha() and m.isupper():
        m = cg.RawExpression(f"MANUFACTURER_{m}")
    else:
        m = hex_int(int(m, 16))

    return m, hex_int(int(t, 16)), hex_int(int(v, 16))


SCHEMA = Schema(
    {
        Required("driver"): {
            Required("name"): str,
            Required("meter_type"): str,
            Optional("info"): str,
            Remove("manufacturer"): str,
            Remove("model"): str,
            Optional("aliases"): str,
            Required("default_fields"): str,
            Required("detect"): {Required("mvt"): ensure_list(CSV_LIST, parse_mvt)},
            Optional("library"): {Required("use"): ensure_list(str)},
            Optional("fields"): {
                Required("field"): ensure_list(
                    Any(
                        NUMERIC_FIELD_WITH_EXTRACTOR,
                        NUMERIC_FIELD_WITH_CALCULATOR_AND_MATCHER,
                        NUMERIC_FIELD_WITH_CALCULATOR,
                        NUMERIC_FIELD,
                        STRING_FIELD_WITH_EXTRACTOR_AND_LOOKUP,
                        STRING_FIELD_WITH_EXTRACTOR,
                        STRING_FIELD,
                    )
                )
            },
            Remove("tests"): object,
            Remove("test"): object,
        }
    }
)


def driver_info_expressions(driver):
    di = ns.di
    yield di.setName(driver["name"])
    if "aliases" in driver:
        yield di.setAliases(driver["aliases"])
    yield di.setMeterType(ns.toMeterType(driver["meter_type"]))
    yield di.setDefaultFields(driver["default_fields"])
    yield from (di.addMVT(*mvt) for mvt in driver["detect"]["mvt"])


def build_matcher(match):
    matcher = ns.class_("FieldMatcher")
    if match is None:
        return matcher()

    matcher = matcher.build()

    if difvifkey := match.get("difvifkey"):
        conv = ns.DifVifKey(difvifkey)
        matcher = matcher.set(conv)
        return matcher

    if measurement_type := match.get("measurement_type"):
        conv = ns.toMeasurementType(measurement_type)
        matcher = matcher.set(conv)

    if vif_range := match.get("vif_range"):
        conv = ns.toVIFRange(vif_range)
        matcher = matcher.set(conv)

    if index_nr := match.get("index_nr"):
        conv = ns.IndexNr(index_nr)
        matcher = matcher.set(conv)

    if storage_nr := match.get("storage_nr"):
        conv = (ns.StorageNr(n) for n in storage_nr)
        matcher = matcher.set(*conv)

    if tariff_nr := match.get("tariff_nr"):
        conv = (ns.TariffNr(n) for n in tariff_nr)
        matcher = matcher.set(*conv)

    if subunit_nr := match.get("subunit_nr"):
        conv = (ns.SubUnitNr(n) for n in subunit_nr)
        matcher = matcher.set(*conv)

    if add_combinable := match.get("add_combinable"):
        conv = ns.toVIFCombinable(add_combinable)
        matcher = matcher.add(conv)

    return matcher


def build_lookup(lookup):
    tns = ns.namespace("Translate")
    lkp = tns.Lookup()
    rule = tns.Rule(
        lookup["name"],
        ns.toMapType(lookup["map_type"]),
    )

    rule = rule.set(ns.MaskBits(lookup["mask_bits"]))
    rule = rule.set(ns.DefaultMessage(lookup["default_message"]))

    for m in lookup["map"]:
        rule = rule.add(tns.Map(m["value"], m["name"], ns.toTestBit(m["test"])))

    lkp = lkp.add(rule)

    return lkp


def constructor_expressions(driver):
    if lib := driver.get("library"):
        for fields in lib["use"]:
            yield ns.addOptionalLibraryFields(fields)

    if fields := driver.get("fields"):
        for field in fields["field"]:
            name = field["name"]
            info = field.get("info", "")
            attributes = ns.toPrintProperties(field["attributes"])
            quantity = ns.toQuantity(field["quantity"])
            calculate = field.get("calculate")
            display_unit = ns.toUnit(field.get("display_unit"))

            match = field.get("match")
            matcher = build_matcher(match)

            if lookup := field.get("lookup"):
                lookup = build_lookup(lookup)

            is_numeric = field["quantity"].casefold() != "text"

            match (is_numeric, bool(calculate), bool(match), bool(lookup)):
                case (True, False, _, _):
                    yield ns.addNumericFieldWithExtractor(
                        name,
                        info,
                        attributes,
                        quantity,
                        ns.toVifScaling(field["vif_scaling"]),
                        ns.toDifSignedness(field["dif_signedness"]),
                        matcher,
                        display_unit,
                        field["force_scale"],
                    )
                case (True, True, False, _):
                    yield ns.addNumericFieldWithCalculator(
                        name,
                        info,
                        attributes,
                        quantity,
                        calculate,
                        display_unit,
                    )
                case (True, True, True, _):
                    yield ns.addNumericFieldWithCalculatorAndMatcher(
                        name,
                        info,
                        attributes,
                        quantity,
                        calculate,
                        matcher,
                        display_unit,
                    )
                case (False, _, _, True):
                    yield ns.addStringFieldWithExtractorAndLookup(
                        name,
                        info,
                        attributes,
                        matcher,
                        lookup,
                    )
                case (False, _, _, False):
                    yield ns.addStringFieldWithExtractor(
                        name,
                        info,
                        attributes,
                        matcher,
                    )


def generate(xmq_content: str):
    raw = parse_xmq(xmq_content)

    data = SCHEMA(raw)

    template_path = Path(__file__).parent / "xmq_driver.cpp.j2"
    template: Template = Template(
        template_path.read_text(),
        trim_blocks=True,
        lstrip_blocks=True,
    )

    return template.render(
        constructor_expressions=(
            str(ExpressionStatement(e)) for e in constructor_expressions(data["driver"])
        ),
        driver_info_expressions=(
            str(ExpressionStatement(e)) for e in driver_info_expressions(data["driver"])
        ),
    )
