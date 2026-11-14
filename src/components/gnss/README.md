# GNSS 组件

一个目录、一个 target：`ps_gnss`。

提供的 `DataSourceSpec`：

| id | 名称 | 传输 |
|---|---|---|
| `data_loader_nmea` | NMEA | file / serial / tcp |
| `data_loader_ubx` | UBX | file / serial |
| `data_source_oem7` | NovAtel OEM7 | file / serial / tcp |

删除本目录后，app 仍可构建运行，只是少了 NMEA/UBX/OEM7 数据源。后续 GNSS 调试 Dock、协议与 Ribbon 页继续加到本组件内。
