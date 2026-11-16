#!/usr/bin/env bash
set -euo pipefail

# Developer-only generator. Generated output is committed; normal builds never
# invoke mavgen or require Python.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEFS="$ROOT/src/3rdparty/mavlink/message_definitions/v1.0"
OUT="$ROOT/src/3rdparty/mavlink/generated"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

mavgen.py --lang=C++11 --wire-protocol=2.0 --output="$TMP" "$DEFS/ardupilotmega.xml"
rm -rf "$OUT/ardupilotmega" "$OUT/common" "$OUT/standard" "$OUT/minimal" \
       "$OUT/uAvionix" "$OUT/icarous" "$OUT/loweheiser" "$OUT/cubepilot" "$OUT/csAirLink"
cp -a "$TMP"/ardupilotmega "$TMP"/common "$TMP"/standard "$TMP"/minimal \
      "$TMP"/uAvionix "$TMP"/icarous "$TMP"/loweheiser "$TMP"/cubepilot "$TMP"/csAirLink "$OUT/"
cp "$TMP"/checksum.h "$TMP"/mavlink_conversions.h "$TMP"/mavlink_get_info.h \
   "$TMP"/mavlink_helpers.h "$TMP"/mavlink_sha256.h "$TMP"/mavlink_types.h \
   "$TMP"/message.hpp "$TMP"/msgmap.hpp "$TMP"/protocol.h "$OUT/"

python3 "$ROOT/tools/mavlink-metadata.py" \
  --dialect ardupilotmega \
  --output "$OUT/mavlink_metadata.json"
