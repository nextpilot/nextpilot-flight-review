#!/usr/bin/env python3
"""Generate nmea_database.json from pynmea2 sentence classes.

The generated file is committed; the application never runs Python at build time.
Run: python3 tools/nmea-metadata.py
"""

import json
from pathlib import Path

import pynmea2
import pynmea2.types.talker as talker

ROOT = Path(__file__).resolve().parent.parent
DATABASE = ROOT / "src/extensions/builtin/gnss/nmea_database.json"

UNIT_BY_NAME = (
    ("altitude", "m"),
    ("geo_sep", "m"),
    ("geoidal", "m"),
    ("height", "m"),
    ("range", "nm"),
    ("speed", "kn"),
    ("spd", "kn"),
    ("course", "deg"),
    ("track", "deg"),
    ("heading", "deg"),
    ("bearing", "deg"),
    ("elevation", "deg"),
    ("azimuth", "deg"),
    ("pdop", "ratio"),
    ("hdop", "ratio"),
    ("vdop", "ratio"),
    ("snr", "dBHz"),
    ("num_sats", "count"),
    ("num_sv", "count"),
    ("circle_rad", "nm"),
)


def infer_unit(name: str) -> str:
    for token, unit in UNIT_BY_NAME:
        if token in name:
            return unit
    return ""


def field_type(name: str, py_type) -> str:
    if py_type in (int,):
        return "int"
    if py_type in (float,) or getattr(py_type, "__name__", "") == "Decimal":
        return "number"
    if getattr(py_type, "__name__", "") in ("timestamp", "datestamp"):
        return "time"
    if name.endswith("_dir") or name.endswith("_sym"):
        return "string"
    return "string"


def convert_field(index: int, definition) -> dict:
    label, name = definition[0], definition[1]
    py_type = definition[2] if len(definition) > 2 else str

    field = {
        "name": name,
        "index": index,
        "type": field_type(name, py_type),
        "label": label,
    }
    unit = infer_unit(name)
    if unit:
        field["unit"] = unit
    if field["type"] == "string" and unit in ("m", "kn", "deg", "ratio", "count", "dBHz", "nm", "km/h", "h", "min"):
        field["type"] = "number"
    if field["type"] == "string" and name in (
        "num_sats",
        "num_sv_in_view",
        "num_messages",
        "age_gps_data",
        "pdop",
        "hdop",
        "vdop",
        "snr_1",
    ):
        field["type"] = "number"

    if name in ("lat", "latitude", "lon", "longitude", "lat_next", "lon_next", "waypoint_lat", "waypoint_lon"):
        field["type"] = "dm"
        field["unit"] = "deg"
        field["hemiIndex"] = index + 1
    return field


def generate() -> dict:
    existing = {}
    if DATABASE.exists():
        old = json.loads(DATABASE.read_text(encoding="utf-8"))
        existing = {sentence["key"]: sentence for sentence in old.get("sentences", [])}

    sentences = dict(existing)
    for key, cls in vars(talker).items():
        if key in existing or not isinstance(cls, type):
            continue
        fields = getattr(cls, "fields", ())
        if not fields:
            continue
        converted = [convert_field(i, definition) for i, definition in enumerate(fields, 1)]
        if converted:
            sentences[key] = {"key": key, "fields": converted}

    return {
        "version": 2,
        "source": f"pynmea2 {pynmea2.__version__}",
        "sentences": [sentences[key] for key in sorted(sentences)],
    }


if __name__ == "__main__":
    document = generate()
    DATABASE.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {DATABASE}: {len(document['sentences'])} sentences")
