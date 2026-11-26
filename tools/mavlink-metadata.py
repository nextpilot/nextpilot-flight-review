#!/usr/bin/env python3
"""Dump MAVLink field units/enums from a pymavlink-generated dialect.

The C headers carry wire layout, offsets, array lengths and CRC extras; this
sidecar only adds the XML metadata that C does not expose.
"""

import argparse
import importlib
import inspect
import json

TYPE_SIZE = {
    "char": 1,
    "uint8_t": 1,
    "int8_t": 1,
    "uint16_t": 2,
    "int16_t": 2,
    "uint32_t": 4,
    "int32_t": 4,
    "float": 4,
    "uint64_t": 8,
    "int64_t": 8,
    "double": 8,
}


def dump_dialect(dialect: str) -> dict:
    module = importlib.import_module(f"pymavlink.dialects.v20.{dialect}")
    message_base = module.MAVLink_message

    messages = []
    for cls in vars(module).values():
        if not inspect.isclass(cls) or not issubclass(cls, message_base):
            continue
        if cls is message_base or not hasattr(cls, "id"):
            continue
        if not getattr(cls, "fieldnames", None):
            continue

        by_name = {}
        for index, name in enumerate(cls.fieldnames):
            by_name[name] = {
                "name": name,
                "type": cls.fieldtypes[index],
                "arrayLength": cls.array_lengths[index],
                "units": cls.fieldunits_by_name.get(name),
                "enum": cls.fieldenums_by_name.get(name),
                "display": cls.fielddisplays_by_name.get(name),
            }

        # MAVLink wire order is type-size descending, stable for equal sizes;
        # pymavlink already exposes that order.
        wire_names = getattr(cls, "ordered_fieldnames", cls.fieldnames)
        offset = 0
        for order, name in enumerate(wire_names):
            field = by_name[name]
            field["wireOffset"] = offset
            field["wireOrder"] = order
            offset += TYPE_SIZE.get(field["type"], 1) * max(1, field["arrayLength"])

        fields = [by_name[name] for name in cls.fieldnames]
        messages.append(
            {
                "id": cls.id,
                "name": getattr(cls, "msgname", cls.__name__),
                "crcExtra": cls.crc_extra,
                "wireLength": offset,
                "fields": fields,
            }
        )

    enums = []
    for name, enum in module.enums.items():
        entries = []
        for value, entry in enum.items():
            entries.append(
                {
                    "value": value,
                    "name": entry.name,
                    "description": entry.description,
                }
            )
        enums.append(
            {
                "name": name,
                "bitmask": enum.bitmask,
                "entries": entries,
            }
        )

    messages.sort(key=lambda item: item["id"])
    enums.sort(key=lambda item: item["name"])
    return {"dialect": dialect, "messages": messages, "enums": enums}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dialect", default="ardupilotmega")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    with open(args.output, "w", encoding="utf-8") as handle:
        json.dump(dump_dialect(args.dialect), handle, ensure_ascii=False, indent=2)
        handle.write("\n")


if __name__ == "__main__":
    main()
