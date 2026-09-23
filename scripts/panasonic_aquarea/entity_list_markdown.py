from pathlib import Path
from string import ascii_letters
from sys import path

from jinja2 import Environment
from voluptuous.schema_builder import Undefined

root_dir = Path(__file__).parents[2]

component_dir = root_dir / "components" / "panasonic_aquarea"
path.insert(0, str(component_dir.parent))

from panasonic_aquarea.platform_descriptor import Platform

Undefined.__repr__ = lambda x: "—"


def id_sort_key(id_val):
    """Order: 12 < 14 < 14:0 < 14:1 < x0."""
    id_str = str(id_val)
    number = id_str.lstrip(ascii_letters)
    main, _, sub = number.partition(":")
    return id_str.removesuffix(number), int(main), int(sub or -1)


platforms = Platform.auto_load()

all_entities = [d for platform in platforms for d in platform._descriptors.values()]
all_entities.sort(key=lambda d: (d._id_field_name(), id_sort_key(d.id_value)))


env = Environment()


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
