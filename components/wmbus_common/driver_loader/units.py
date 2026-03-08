import string
from functools import cache
from pathlib import Path


@cache
def units_dict():
    """Builds a dictionary of units from the units.h file."""
    units = {}
    with Path(__file__).parent.with_name("units.h").open() as file:
        for line in file:
            if "LIST_OF_UNITS" in line:
                break

        for line in file:
            if line.strip() == "":
                break

            line = (
                line.strip(string.whitespace + "\\")
                .removeprefix("X(")
                .removesuffix(")")
            )
            if line:
                line = line.split(",")
                if len(line) == 5:
                    cname, lcname, hrname, quantity, explanation = line
                    units[lcname] = hrname.strip('"')
                else:
                    break

    return units


def split_name_unit(field_with_suffix):
    parts = field_with_suffix.rsplit("_", 1)
    if len(parts) == 1 or parts[-1] not in units_dict():
        return field_with_suffix, ""
    else:
        return parts


def get_human_readable_unit(field_unit: str):
    _, unit = split_name_unit(field_unit)
    return units_dict().get(unit.lower(), unit or "?")
