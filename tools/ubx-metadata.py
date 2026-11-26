#!/usr/bin/env python3
"""Generate ubx_database.json from pyubx2 payload dictionaries.

Run: python3 tools/ubx-metadata.py
"""

import json
import re
from pathlib import Path

import pyubx2
from pyubx2.ubxhelpers import GNSSLIST, SIGID
from pyubx2.ubxtypes_core import UBX_MSGIDS
from pyubx2.ubxtypes_get import UBX_PAYLOADS_GET
from pyubx2.ubxtypes_set import UBX_PAYLOADS_SET

ROOT = Path(__file__).resolve().parent.parent
DATABASE = ROOT / "src/extensions/builtin/gnss/ubx_database.json"

# pyubx2 variant names not present in UBX_MSGIDS
VARIANT_BASE = {
    "AID-ALP-ACK": "AID-ALP",
    "AID-ALPSRV-REQ": "AID-ALPSRV",
    "AID-ALPSRV-SEND": "AID-ALPSRV",
    "CFG-DAT-NUM": "CFG-DAT",
    "CFG-NMEAv0": "CFG-NMEA",
    "CFG-NMEAvX": "CFG-NMEA",
    "CFG-TP5-TPX": "CFG-TP5",
    "MON-RF-V0": "MON-RF",
    "NAV-AOPSTATUS-L": "NAV-AOPSTATUS",
    "NAV-DAHEADINGHP": "NAV-DAHEADING",
    "NAV-RELPOSNED-V0": "NAV-RELPOSNED",
    "RXM-PMP-V0": "RXM-PMP",
    "RXM-PMP-V1": "RXM-PMP",
    "RXM-PMREQ-S": "RXM-PMREQ",
    "RXM-RLM-S": "RXM-RLM",
    "RXM-RLM-L": "RXM-RLM",
    "SEC-SIG-V1": "SEC-SIG",
    "SEC-SIG-V2": "SEC-SIG",
    "SEC-UNIQID-V2": "SEC-UNIQID",
    "TIM-VCOCAL-V0": "TIM-VCOCAL",
}

VARIANT_RULES = {
    "AID-ALP-ACK": {"length": 1},
    "AID-ALPSRV-REQ": {"offset": 1, "type": "u1", "not_equals": 0xFF},
    "AID-ALPSRV-SEND": {"offset": 1, "type": "u1", "equals": 0xFF},
    "CFG-NMEAvX": {"length": 4},
    "CFG-NMEAv0": {"length": 12},
    "MON-RF-V0": {"length_base": 4, "length_count_offset": 1, "length_stride": 16},
    "NAV-AOPSTATUS-L": {"length": 20},
    "NAV-DAHEADINGHP": {"offset": 0, "type": "u1", "not_equals": 2},
    "NAV-RELPOSNED-V0": {"offset": 0, "type": "u1", "equals": 0},
    "RXM-PMP-V0": {"offset": 0, "type": "u1", "equals": 0},
    "RXM-PMP-V1": {"offset": 0, "type": "u1", "not_equals": 0},
    "RXM-RLM-S": {"offset": 1, "type": "u1", "equals": 1},
    "RXM-RLM-L": {"offset": 1, "type": "u1", "not_equals": 1},
    "SEC-SIG-V1": {"offset": 0, "type": "u1", "equals": 1},
    "SEC-SIG-V2": {"offset": 0, "type": "u1", "not_equals": 1},
    "SEC-UNIQID-V2": {"length": 10},
}

UNIT_RULES = (
    ("i_tow", "s"),
    ("tow", "s"),
    ("time_tag", "s"),
    ("lon", "deg"),
    ("lat", "deg"),
    ("height", "m"),
    ("h_msl", "m"),
    ("h_acc", "m"),
    ("v_acc", "m"),
    ("rel_pos", "m"),
    ("acc_n", "m"),
    ("acc_e", "m"),
    ("acc_d", "m"),
    ("acc_length", "m"),
    ("vel_n", "m/s"),
    ("vel_e", "m/s"),
    ("vel_d", "m/s"),
    ("g_speed", "m/s"),
    ("s_acc", "m/s"),
    ("head_mot", "deg"),
    ("head_veh", "deg"),
    ("head_acc", "deg"),
    ("acc_heading", "deg"),
    ("heading", "deg"),
    ("elev", "deg"),
    ("azim", "deg"),
    ("p_dop", "ratio"),
    ("cno", "dBHz"),
    ("pr_res", "m"),
    ("noise_per_ms", "dBHz"),
    ("agc_cnt", "count"),
)


def snake(name: str) -> str:
    name = name.replace("-", "_")
    if name.startswith("_"):
        name = "hp_" + name[1:]
    name = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name)
    name = re.sub(r"_+", "_", name).strip("_").lower()
    return {"lon": "longitude", "lat": "latitude"}.get(name, name)


def type_info(adef):
    scale = 1.0
    if isinstance(adef, tuple):
        adef, scale = adef[0], float(adef[1])
    if adef == "CH":
        return "str", 0, scale
    if adef == "UPMP":
        return "bytes", 0, scale
    size = int(adef[1:4])
    prefix = adef[:1]
    if adef == "CH":
        kind = "str"
    elif adef == "UPMP":
        kind, size = "bytes", 0
    elif prefix == "R":
        kind = "r4" if size == 4 else "r8"
    elif prefix == "C":
        kind = "str"
    elif prefix in ("U", "I", "E", "L", "X", "A", "Y", "Z"):
        if prefix == "A":
            kind = "bytes"
        elif prefix in ("U", "I", "E", "X") and size <= 8:
            kind = ("u" if prefix in ("U", "E", "X") else "i") + str(size)
        elif prefix == "L":
            kind, size = "u1", 1
        else:
            kind = "bytes"
    else:
        kind = "bytes"
    return kind, size, scale


def unit_for(name: str) -> str:
    for token, unit in UNIT_RULES:
        if token in name:
            return unit
    return ""


def enum_for(name: str, enum_by: str = ""):
    if name == "gnss_id" and not enum_by:
        return {"values": {str(k): v for k, v in GNSSLIST.items()}}
    if name == "sig_id" and enum_by:
        values: dict[str, dict[str, str]] = {}
        for (gnss, sig), label in SIGID.items():
            values.setdefault(str(gnss), {})[str(sig)] = label
        return {"by": enum_by, "values": values}
    return None


def size_of(field) -> int:
    if field.get("type") == "bit":
        return 0
    if field["type"] == "group":
        if field.get("count") is not None:
            return field["count"] * field["stride"]
        return 0
    return field.get("size", 0)


def bitfield_fields(name, adef, offset):
    btype, bdict = adef
    raw_type, raw_size, _ = type_info(btype)
    fields = [
        {
            "name": snake(name[:-4]),
            "offset": offset,
            "type": raw_type,
            "size": raw_size,
        }
    ]
    bit = 0
    for flag_name, flag_type in bdict.items():
        width = int(flag_type[1:4]) if isinstance(flag_type, str) else 1
        if not flag_name.startswith("reserved"):
            fields.append(
                {
                    "name": snake(flag_name),
                    "offset": offset,
                    "type": "bit",
                    "size": raw_size,
                    "bitOffset": bit,
                    "bitWidth": width,
                }
            )
        bit += width
    return fields


def build_fields(schema: dict, base: int = 0, prefix: str = ""):
    fields = []
    offset = base
    for name, adef in schema.items():
        if isinstance(adef, tuple) and isinstance(adef[1], dict):
            if isinstance(adef[0], str) and adef[0].startswith("X"):
                fields.extend(bitfield_fields(name, adef, offset))
                offset += type_info(adef[0])[1]
                continue

            count_name, group_schema = adef
            group_fields = build_fields(group_schema, 0)
            stride = sum(size_of(field) for field in group_fields)
            group = {
                "name": snake(name[:-4]),
                "type": "group",
                "offset": offset,
                "stride": stride,
                "fields": group_fields,
            }
            if isinstance(count_name, int):
                group["count"] = count_name
            elif count_name != "None":
                group["countField"] = snake(count_name)
            fields.append(group)
            if isinstance(count_name, int):
                offset += count_name * stride
            elif count_name == "None":
                pass
            else:
                pass
            continue

        ftype, fsize, scale = type_info(adef)
        field = {"name": snake(name), "offset": offset, "type": ftype}
        if fsize:
            field["size"] = fsize
        if scale != 1.0:
            field["scale"] = scale
        unit = unit_for(field["name"])
        if unit:
            field["unit"] = unit
        if field["name"] == "gnss_id":
            field["enum"] = enum_for("gnss_id")
        elif field["name"] == "sig_id":
            field["enum"] = enum_for("sig_id", "gnss_id")
        fields.append(field)
        offset += fsize

    if not prefix:
        return fields
    for field in fields:
        field["name"] = prefix + field["name"]
    return fields


def key_for(name: str):
    if name in UBX_MSGIDS.values():
        for key, value in UBX_MSGIDS.items():
            if value == name:
                return key
    base = VARIANT_BASE.get(name)
    if not base:
        return None
    for key, value in UBX_MSGIDS.items():
        if value == base:
            return key
    return None


def generate():
    schemas = dict(UBX_PAYLOADS_GET)
    for name, schema in UBX_PAYLOADS_SET.items():
        schemas.setdefault(name, schema)
    messages = []
    for name, schema in schemas.items():
        if name in ("UBX-NOMINAL", "FOO-BAR") or not schema:
            continue
        key = key_for(name)
        if not key or len(key) < 2:
            continue
        cls, mid = int.from_bytes(key[0:1], "little"), int.from_bytes(key[1:2], "little")
        fields = build_fields(schema)
        message = {
            "class": cls,
            "id": mid,
            "name": snake(name),
            "fields": fields,
        }
        if "iTOW" in schema:
            message["timeField"] = "i_tow"
        if len(key) >= 3:
            message["variant"] = {"offset": 0, "type": "u1", "equals": int.from_bytes(key[2:3], "little")}
        elif name in VARIANT_RULES:
            message["variant"] = VARIANT_RULES[name]
        if name in VARIANT_BASE:
            message["baseName"] = snake(VARIANT_BASE[name])
        messages.append(message)

    messages.sort(key=lambda m: (m["class"], m["id"], m["name"]))
    return {
        "version": 2,
        "source": f"pyubx2 {pyubx2.__version__} (GET/SET field schema; units inferred)",
        "authoritative": "u-blox Interface Description",
        "messages": messages,
    }


if __name__ == "__main__":
    document = generate()
    DATABASE.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {DATABASE}: {len(document['messages'])} messages")
