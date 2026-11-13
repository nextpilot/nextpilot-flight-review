# Third-party data

`oem7_database.json` is copied from the NovAtel EDIE project:

- Source: https://github.com/novatel/novatel_edie (`database/database.json`)
- Copyright (c) 2023 NovAtel Inc.
- License: MIT

It is used as a data-driven OEM7 protocol catalog. The PlotScope OEM7 source
selects a message definition by log name and header CRC, then maps the ASCII
body to channels. No EDIE decoder source is vendored.
