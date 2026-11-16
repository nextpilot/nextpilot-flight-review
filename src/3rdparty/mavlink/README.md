# MAVLink generated library

Vendored from upstream MAVLink `mavgen.py`. The generator itself is not
included in the product; only generated headers, XML definitions and the
metadata sidecar are kept so offline builds do not need Python.

- pymavlink: 2.4.50
- generated dialect: `ardupilotmega` (includes `common`, `standard`, `minimal`)
- wire protocol: MAVLink 2.0
- language: **C++11**
- generator command: `tools/mavlink-gen.sh`
- generated metadata: `generated/mavlink_metadata.json`

Entry points:

- `generated/message.hpp` — `mavlink::Message`, `mavlink::MsgMap`
- `generated/ardupilotmega/ardupilotmega.hpp` — typed message classes and
  `MESSAGE_ENTRIES`
- `generated/ardupilotmega/mavlink.h` — C-compatible dialect header shipped by
  the generator

Framing uses `mavlink::mavlink_frame_char_buffer()` with caller-owned
`mavlink_message_t` and `mavlink_status_t`, so each data source has independent
parser state and no shared channel/status global. Full-protocol field decoding
is driven by the generated metadata sidecar, not by a hand-written
`switch (msgid)`.

License: the generator is LGPLv3; generated code has the MIT exception. See
`COPYING`.
