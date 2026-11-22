#!/usr/bin/env python3
"""生成 ROS2 bag fixture（demo_rosbag2.db3）。

两个 topic：
  /imu   sensor_msgs/msg/Imu          100 条，10 ms，四元数固定、angular_velocity.x 递增
  /speed std_msgs/msg/Float64         100 条，10 ms，data = i * 0.5

CDR 写入规则与解析器（ng/extensions/rosbag2/Rosbag2Parser.cpp 的 Cdr）一致：
头 4 字节表示标识（小端）；字段按 1/2/4/8 对齐，对齐基准是 pos-4。
"""
import sqlite3
import struct
from pathlib import Path

LE = b"\x00\x01\x00\x00"


class Cdr:
    def __init__(self):
        self.buf = bytearray(LE)
        self.pos = 4

    def align(self, a):
        rel = self.pos - 4
        self.pos += (a - rel % a) % a
        while len(self.buf) < self.pos:
            self.buf.append(0)

    def u8(self, v):
        self.buf.append(v & 0xFF)
        self.pos += 1

    def u32(self, v):
        self.align(4)
        self.buf += struct.pack("<I", v & 0xFFFFFFFF)
        self.pos += 4

    def f32(self, v):
        self.align(4)
        self.buf += struct.pack("<f", v)
        self.pos += 4

    def f64(self, v):
        self.align(8)
        self.buf += struct.pack("<d", v)
        self.pos += 8

    def string(self, s):
        b = s.encode()
        self.u32(len(b) + 1)          # ROS2 string 长度含结尾 NUL
        self.buf += b + b"\x00"
        self.pos += len(b) + 1

    def bytes(self):
        return bytes(self.buf)


def header(c, frame_id="imu"):
    c.u32(0)          # sec
    c.u32(0)          # nanosec
    c.string(frame_id)


def imu(c, ax):
    header(c)
    for v in (0.0, 0.0, 0.0, 1.0):   # orientation x,y,z,w
        c.f64(v)
    for _ in range(9):               # orientation_covariance
        c.f64(0.0)
    for v in (ax, 0.0, 0.0):         # angular_velocity
        c.f64(v)
    for _ in range(9):
        c.f64(0.0)
    for v in (0.0, 0.0, 9.8):        # linear_acceleration
        c.f64(v)
    for _ in range(9):
        c.f64(0.0)
    return c.bytes()


def main():
    out = Path(__file__).with_name("demo_rosbag2.db3")
    if out.exists():
        out.unlink()
    conn = sqlite3.connect(out)
    cur = conn.cursor()
    cur.execute("CREATE TABLE topics (id INTEGER PRIMARY KEY, name TEXT, type TEXT, "
                "serialization_format TEXT, offered_qos_profiles TEXT)")
    cur.execute("CREATE TABLE messages (id INTEGER PRIMARY KEY, topic_id INTEGER, "
                "timestamp INTEGER, data BLOB)")
    cur.execute("INSERT INTO topics VALUES (1, '/imu', 'sensor_msgs/msg/Imu', 'cdr', '')")
    cur.execute("INSERT INTO topics VALUES (2, '/speed', 'std_msgs/msg/Float64', 'cdr', '')")

    mid = 0
    for i in range(100):
        mid += 1
        c = Cdr()
        cur.execute("INSERT INTO messages VALUES (?,?,?,?)",
                    (mid, 1, i * 10_000_000, imu(c, 0.01 * i)))
        mid += 1
        c = Cdr()
        c.f64(i * 0.5)
        cur.execute("INSERT INTO messages VALUES (?,?,?,?)",
                    (mid, 2, i * 10_000_000, c.bytes()))
    conn.commit()
    conn.close()
    print("wrote", out, out.stat().st_size, "bytes")


if __name__ == "__main__":
    main()
