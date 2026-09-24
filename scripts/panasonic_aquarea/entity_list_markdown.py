import re
from pathlib import Path
from sys import path

import esphome.config_validation as cv
from jinja2 import Environment

root_dir = Path(__file__).parents[2]

component_dir = root_dir / "components" / "panasonic_aquarea"
path.insert(0, str(component_dir.parent))

from panasonic_aquarea.platform_descriptor import Platform


def id_sort_key(id_val):
    """Natural order: 5 < 5:direct < 12 < 16:2 < 16:10 < x0."""
    parts = re.split(r"(\d+)", str(id_val))
    return [int(part) if part.isdigit() else part for part in parts]


platforms = Platform.auto_load()

all_entities = [d for platform in platforms for d in platform._descriptors.values()]
all_entities.sort(key=lambda d: (d._id_field_name(), id_sort_key(d.id_value)))


env = Environment(finalize=lambda value: "-" if value is cv.UNDEFINED else value)


# Create entity dictionaries by platform type
entity_vars = {}

for e in all_entities:
    entity_vars.setdefault(f"{e.platform.name}_entities", []).append(e)


for template in Path(__file__).parent.glob("*.jinja2"):
    tmpl = env.from_string(template.read_text())
    output_path = root_dir / "docs" / "panasonic_aquarea" / "entities" / template.stem
    output_path.write_text(
        tmpl.render(
            all_entities=all_entities,
            **entity_vars,
        )
    )
    print(f"Generated: {output_path.name}")
