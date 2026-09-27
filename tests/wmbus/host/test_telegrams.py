"""Replays the XMQ drivers' tests{} telegrams through the host build against upstream's JSON.

Needs esphome importable: run it from the esphome venv.
"""

import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from functools import cache
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[2]
COMPONENTS_DIR = REPO_ROOT / "components"
DRIVERS_DIR = COMPONENTS_DIR / "wmbus_common" / "drivers" / "src"
CONFIG = HERE / "test.yaml"
BINARY = HERE / ".esphome" / "build" / "wmbus-host-test" / ".pioenvs" / "wmbus-host-test" / "program"

sys.path.insert(0, str(COMPONENTS_DIR))

# The runner answers exactly one line per case, in input order.
RESULT_PREFIX = "@@WMBUS@@ "
ERROR_PREFIX = "@@WMBUS-ERR@@ "

# What upstream's JSON carries that the runner does not print.
ENVELOPE_KEYS = {"_", "driver", "id", "name", "timestamp", "rssi_dbm"}

# Each traced to upstream's own code; the xfail is strict, so a fix must delete its line.
KNOWN_DECODE_DIFFS = {
    "actislink-ANYID": "addressed as ANYID and encrypted with DES",
    "hcae2-54423117#4": "delta_14_half_months_ago_hca untraced",
}

# Overlapping mvt lines: `type: auto` picks whichever driver the registry holds first.
KNOWN_DETECTION_DIFFS = {
    "actislink-ANYID": "addressed as ANYID and encrypted with DES",
    "mkradio4a-01770002": "same mvt line as weh_07",
}

# Upstream separates frame layers for readability; stripping here keeps the decoder's hex parsing strict.
SEPARATORS = str.maketrans("", "", " |#_-")


@dataclass
class Case:
    id: str
    driver: str
    meter_id: str
    key: str
    telegrams: list[str]
    expected: dict
    comment: str


def split_telegrams(value: str) -> list[str]:
    """Upstream separates a test's frames by commas or by newlines."""
    parts = (part.translate(SEPARATORS) for part in re.split(r"[,\n]", value))
    return [part for part in parts if part]


def as_wireless(frame: bytes) -> bytes:
    """A wired M-Bus frame as a radio would hear it; every wired test frame has a long TPL header (CI 72)."""
    tpl = frame[6:-2]
    meter_id, mfct, version_type = tpl[1:5], tpl[5:7], tpl[7:9]
    body = bytes([0x44]) + mfct + meter_id + version_type + tpl
    return bytes([len(body)]) + body


# The bytes an ELL header takes after its CI.
ELL_LENGTH = {0x8C: 2, 0x8D: 8, 0x8E: 10, 0x8F: 16}


# Diehl's manufacturer codes, as in quirks.cpp.
DIEHL = {"DME", "EWT", "HYD", "SAP", "SPL"}


def diehl_real_data_log(frame: bytes) -> bool:
    """A decrypted Diehl LFSR real-data log (CI 7A, mode >= 16): its clear payload sums to the TPL config's check."""
    from wmbus_common.drivers.loader.xmq_loader import manufacturer_code

    return (
        len(frame) > 15
        and (frame[3] << 8 | frame[2]) in {manufacturer_code(m) for m in DIEHL}
        and frame[1] in (0x44, 0x46)
        and frame[10] == 0x7A
        and frame[14] & 0x10
        and sum(frame[15:]) & 0xEF == frame[14] & 0xEF
    )


def crc16_en13757(data: bytes) -> int:
    crc = 0
    for byte in data:
        for _ in range(8):
            if ((crc >> 8) & 0x80) ^ (byte & 0x80):
                crc = ((crc << 1) ^ 0x3D65) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
            byte = (byte << 1) & 0xFF
    return (~crc) & 0xFFFF


def declassified(frame: bytes) -> bytes:
    """Upstream's already-decrypted logs whose headers still announce encryption, as the plain frames they are.

    Drops the security mode and what only an encrypted TPL has (2F2F, mode 7's extra byte, Diehl's
    reordered address). A frame whose ELL CRC fails is really encrypted and stays.
    """
    if diehl_real_data_log(frame):
        # Real data puts the DLL's version and type in front of the id.
        return frame[:4] + frame[6:10] + frame[4:6] + frame[10:14] + bytes([frame[14] & 0xE0]) + frame[15:]

    out = bytearray(frame)
    pos = 10
    ell_crc = None
    if pos < len(out) and out[pos] in ELL_LENGTH:
        ci = out[pos]
        if ci in (0x8D, 0x8F):
            session = pos + (3 if ci == 0x8D else 11)
            ell_crc = session + 4
            if int.from_bytes(out[ell_crc : ell_crc + 2], "little") != crc16_en13757(out[ell_crc + 2 :]):
                return frame
            out[session + 3] &= 0x1F  # the mode, in the session number's top three bits
        pos += 1 + ELL_LENGTH[ci]
    if pos < len(out) and out[pos] == 0x81:  # NWL
        pos += 2
    if pos < len(out) and out[pos] == 0x90:  # AFL
        pos += 2 + out[pos + 1]
    if pos < len(out) and out[pos] in (0x72, 0x7A):
        cfg = pos + (11 if out[pos] == 0x72 else 3)
        mode = out[cfg + 1] & 0x1F
        records = cfg + 2 + (mode == 7)
        if mode in (5, 7) and out[records : records + 2] == b"\x2f\x2f":
            out[cfg + 1] &= 0xE0
            del out[records : records + 2]
            if mode == 7:
                del out[cfg + 2]
            out[0] -= 2 + (mode == 7)
    if ell_crc is not None:
        out[ell_crc : ell_crc + 2] = crc16_en13757(out[ell_crc + 2 :]).to_bytes(2, "little")
    return bytes(out)


def as_heard(telegram: str) -> str:
    """The telegram as a radio delivers it. Whole bytes only: one upstream telegram ends in half a byte."""
    frame = bytes.fromhex(telegram[: len(telegram) // 2 * 2])
    if frame[0] == 0x68 and frame[3] == 0x68 and frame[1] == frame[2]:
        frame = as_wireless(frame)
    return declassified(frame).hex().upper()


def as_list(value) -> list:
    if value is None:
        return []
    return value if isinstance(value, list) else [value]


def collect_cases() -> list[Case]:
    from wmbus_common.drivers.loader.xmq import parse_xmq

    cases = []
    seen: dict[str, int] = {}
    for path in sorted(DRIVERS_DIR.glob("*.xmq")):
        definition = parse_xmq(path.read_text())["driver"]
        for test in as_list((definition.get("tests") or {}).get("test")):
            args = test.get("args", "").split()
            if len(args) != 4 or "telegram" not in test or "json" not in test:
                continue

            _name, driver, meter_id, key = args
            try:
                expected = json.loads(test["json"])
            except json.JSONDecodeError:
                continue

            name = f"{driver}-{meter_id}"
            seen[name] = seen.get(name, 0) + 1
            if seen[name] > 1:
                name = f"{name}#{seen[name]}"

            cases.append(
                Case(
                    id=name,
                    driver=driver,
                    meter_id=meter_id,
                    key=key,
                    telegrams=[as_heard(t) for t in split_telegrams(test["telegram"])],
                    expected=expected,
                    comment=" ".join(as_list(test.get("comment"))),
                )
            )
    return cases


def driver_names() -> dict[str, str]:
    """Every driver name this build knows, aliases included, mapped to the driver it resolves to."""
    from wmbus_common.drivers.loader.driver_manager import DriverManager as Manager

    manager = Manager()
    manager.load_drivers()
    return {name: manager.request_driver(name).name for name in manager.available_drivers}


NAMES = driver_names()
CASES = collect_cases()


def parameters(known_diffs: dict[str, str]):
    """Skips cases of drivers upstream still ships as C++ (qwater in qwaterv2.xmq)."""
    params = []
    for case in CASES:
        marks = []
        if case.driver not in NAMES:
            marks.append(pytest.mark.skip(reason=f"no driver {case.driver} in this build"))
        elif case.id in known_diffs:
            marks.append(pytest.mark.xfail(reason=known_diffs[case.id], strict=True))
        params.append(pytest.param(case, marks=marks, id=case.id))
    return params


def run_batch(as_auto: bool) -> dict[str, str]:
    """as_auto sends every case to a meter without a driver, as `type: auto` leaves one."""
    cases = [c for c in CASES if c.driver in NAMES]
    stdin = "".join(
        "\t".join(
            ["auto" if as_auto else NAMES[c.driver], c.meter_id, c.key, ",".join(c.telegrams)]
        )
        + "\n"
        for c in cases
    )
    result = subprocess.run(
        [str(BINARY)],
        input=stdin,
        capture_output=True,
        text=True,
        env={**os.environ, "TZ": "UTC"},
    )

    answers = [
        line
        for line in result.stdout.splitlines()
        if line.startswith(RESULT_PREFIX) or line.startswith(ERROR_PREFIX)
    ]
    if len(answers) != len(cases):
        pytest.fail(
            f"runner answered {len(answers)} of {len(cases)} cases "
            f"(exit {result.returncode})\n{result.stderr.strip()[:2000]}"
        )
    return {case.id: answer for case, answer in zip(cases, answers)}


@pytest.fixture(scope="session")
def binary(request) -> Path:
    if not request.config.getoption("--no-build"):
        subprocess.run(
            [sys.executable, "-m", "esphome", "compile", CONFIG.name], cwd=HERE, check=True
        )
    if not BINARY.exists():
        pytest.fail(f"no binary at {BINARY}; drop --no-build")
    return BINARY


@pytest.fixture(scope="session")
def decoded(binary) -> dict[str, str]:
    return run_batch(as_auto=False)


@pytest.fixture(scope="session")
def detected(binary) -> dict[str, str]:
    return run_batch(as_auto=True)


def expected_fields(expected: dict) -> dict:
    """Upstream's JSON without the envelope and the synthetic "<field>_deprecated_by" warnings."""
    return {k: v for k, v in expected.items() if k not in ENVELOPE_KEYS and not k.endswith("_deprecated_by")}


@cache
def hidden_fields(driver: str) -> set[str]:
    """The names upstream leaves out of its JSON: those whose every definition is HIDE."""
    from wmbus_common.drivers.loader.xmq import parse_xmq
    from wmbus_common.drivers.loader.xmq_loader import SCHEMA, Attribute, concrete_fields

    path = DRIVERS_DIR / f"{driver}.xmq"
    hidden: dict[str, bool] = {}
    for f in concrete_fields(SCHEMA(parse_xmq(path.read_text()))["driver"]):
        hidden[f.json_name] = hidden.get(f.json_name, True) and Attribute.Hide in f.definition.attributes
    return {name for name, is_hidden in hidden.items() if is_hidden}


def upstream_view(driver: str, fields: dict) -> dict:
    """The runner's fields as upstream prints them: HIDE ones dropped."""
    return {
        name: value
        for name, value in fields.items()
        if name not in hidden_fields(driver) and name not in ENVELOPE_KEYS
    }


def same_value(actual, expected) -> bool:
    """Upstream prints six decimals, ArduinoJson nine significant digits."""
    if isinstance(actual, (int, float)) and isinstance(expected, (int, float)):
        return actual == pytest.approx(expected, rel=1e-6, abs=1e-6)
    return actual == expected


def answer_of(line: str) -> tuple[str, dict]:
    assert not line.startswith(ERROR_PREFIX), line[len(ERROR_PREFIX) :]
    driver, _, fields = line[len(RESULT_PREFIX) :].partition("\t")
    try:
        return driver, json.loads(fields)
    except json.JSONDecodeError:
        pytest.fail(f"not JSON: {fields[:200]}")


@pytest.mark.parametrize("case", parameters(KNOWN_DECODE_DIFFS))
def test_decode(case: Case, decoded: dict[str, str]) -> None:
    actual, expected = upstream_view(*answer_of(decoded[case.id])), expected_fields(case.expected)

    missing = {k: v for k, v in expected.items() if k not in actual}
    extra = {k: v for k, v in actual.items() if k not in expected}
    wrong = {
        k: f"{actual[k]!r} != {v!r}"
        for k, v in expected.items()
        if k in actual and not same_value(actual[k], v)
    }
    if not (missing or extra or wrong):
        return

    parts = []
    if wrong:
        parts.append(f"wrong={wrong}")
    if missing:
        parts.append(f"missing={missing}")
    if extra:
        parts.append(f"extra={extra}")
    if case.comment:
        parts.append(case.comment)
    pytest.fail("; ".join(parts))


@pytest.mark.parametrize("case", parameters(KNOWN_DETECTION_DIFFS))
def test_detection(case: Case, detected: dict[str, str]) -> None:
    driver, _ = answer_of(detected[case.id])
    assert NAMES.get(driver, driver) == NAMES.get(case.driver, case.driver), (
        f"detected as {driver!r}"
    )
