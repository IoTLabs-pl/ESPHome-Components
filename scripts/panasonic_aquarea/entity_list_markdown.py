from importlib import import_module
from pathlib import Path
from sys import path
from jinja2 import Environment
from voluptuous.schema_builder import Undefined

root_dir = Path(__file__).parents[2]

component_dir = root_dir / "components" / "panasonic_aquarea"
path.insert(0, str(component_dir.parent))

from panasonic_aquarea.platform_descriptor import Platform


for desc in component_dir.glob("*/descriptors.yaml"):
    module_name = desc.parent.name
    import_module(component_dir.name + "." + module_name)


def format_id(value):
    """Format ID value: if it's a number with 3+ digits, insert underscore after 2nd digit."""
    s = str(value)
    if s.isdigit() and s.startswith("16") and len(s) >= 3:
        return f"{s[:2]}_{s[2:]}"
    return s


def format_numeric_key(id_val):
    """Format numeric part of ID for sorting - returns string like 001, 016001."""
    if isinstance(id_val, int):
        return str(id_val).zfill(3)
    elif isinstance(id_val, str):
        # Replace underscore with zero-padded numbers: '16_1' -> '016001'
        if "_" in id_val:
            parts = id_val.split("_")
            return parts[0].zfill(3) + parts[1].zfill(3)
        else:
            return id_val.zfill(3)
    else:
        return "999999"


def all_entities_sorted(platforms):
    """Collect and sort all entities from all platforms."""
    all_items = [
        (platform, id_val, desc)
        for platform in platforms
        for id_val, desc in platform._descriptors.items()
    ]

    return sorted(
        all_items,
        key=lambda item: item[2]._id_field_name().upper() + format_numeric_key(item[1]),
    )


def get_platform(platforms, name):
    """Get a platform by name."""
    for platform in platforms:
        if platform.name == name:
            return platform
    return None


Undefined.__repr__ = lambda x: "—"

env = Environment()
env.filters["format_id"] = format_id
env.globals["all_entities_sorted"] = all_entities_sorted
env.globals["get_platform"] = get_platform

for template_name in [
    "id",
    "name",
    "entity_type",
]:
    template_path = Path(__file__).with_name(f"by_{template_name}.md.jinja2")
    tmpl = env.from_string(template_path.read_text())
    output_path = (
        root_dir / "docs" / "panasonic_aquarea" / "entities" / template_path.name
    ).with_suffix("")
    output_path.write_text(tmpl.render(platforms=Platform.REGISTERED_PLATFORMS))
    print(f"Generated: {output_path.name}")
