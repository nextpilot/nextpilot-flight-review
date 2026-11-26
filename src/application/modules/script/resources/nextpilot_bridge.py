# nextpilot_bridge.py —— PlotScope NG Python 子进程桥（M2.6，路线 B）
#
# 由宿主以 `python -m nextpilot_bridge` 启动（模块目录经 PYTHONPATH 注入）。
# 协议：stdin/stdout JSON-line（一行一请求、一行一响应），元数据走 JSON，
# 大数组走文件旁路（宿主侧 /dev/shm 或临时目录 + np.memmap）——100 万行
# double 只有一次 8MB 的 memcpy，没有 JSON 序列化灾难。
#
# 请求：{"id":N,"op":...,...}
#   eval  {code}                        -> value = eval(code)（表达式的 JSON 值）
#   exec  {source, cols?, out_dir?}     -> {value, out, new}
#       cols:    {列名: {path, rows}}  —— 宿主预先写好的 f64 数组文件，
#                 组装成 ds 对象注入（ds['col'] -> np.ndarray；ds['new']=... 收集新列）
#       out_dir: 新列 .bin 的写出目录（宿主创建）
#   put   {name, path, rows}            -> 把 f64 文件注册进全局（np.memmap 读视图）
#   pull  {name, path}                  -> np.asarray(globals[name]).tofile(path) -> {rows}
# 响应：{"id":N,"ok":true,...} / {"id":N,"ok":false,"error":"...","out":"..."}
# 任何行都不写 stdout 以外的内容（stderr 只给宿主诊断用）。
#
# 设计对齐 roadmap：常驻进程（状态跨请求保留）、崩溃由宿主重启兜底、
# 每条请求由宿主侧超时控制（超时杀进程 = 强制中断）。

import ast
import contextlib
import io
import json
import sys

try:
    import numpy as np
except ImportError:  # 桥的大数组旁路（memmap / tofile）硬依赖 numpy
    sys.stderr.write(
        "[bridge] 缺少 numpy：PlotScope 的 Python 桥需要它（ds 列注入 / 写回走 "
        "np.memmap 与 tofile）。安装方式：apt install python3-numpy 或 pip install numpy\n"
    )
    raise


class Ds:
    """数据集视图：宿主 push 进来的列 + 脚本写回收集。

    ds['col']      -> np.ndarray（只读语义约定，改动不回传）
    ds['new'] = v  -> 收集为写回列（exec 结束后宿主取走落成 v2 数据集）
    ds.keys()      -> 已有列名
    """

    def __init__(self, cols):
        self._cols = dict(cols)
        self._new = {}

    def __getitem__(self, k):
        return self._cols[k]

    def __setitem__(self, k, v):
        self._new[str(k)] = np.asarray(v, dtype="<f8")

    def __contains__(self, k):
        return k in self._cols

    def keys(self):
        return list(self._cols.keys())


GLOBALS = {"np": np, "Ds": Ds}


def _jsonable(v):
    """递归转 JSON 安全值（numpy 标量/数组 -> python 原生）。"""
    if v is None or isinstance(v, (bool, str)):
        return v
    if isinstance(v, (int,)):
        return v
    if isinstance(v, float):
        if v != v or v in (float("inf"), float("-inf")):
            return str(v)
        return v
    if isinstance(v, np.generic):
        return _jsonable(v.item())
    if isinstance(v, np.ndarray):
        return [_jsonable(x) for x in v.tolist()]
    if isinstance(v, (list, tuple)):
        return [_jsonable(x) for x in v]
    if isinstance(v, dict):
        return {str(k): _jsonable(x) for k, x in v.items()}
    return repr(v)


def _load_col(meta):
    """宿主写好的 f64 文件 -> ndarray（memmap 惰性映射，不复制）。"""
    return np.memmap(meta["path"], dtype="<f8", mode="r", shape=(int(meta["rows"]),))


def _run_source(src, g):
    """exec 整段源码；若末条语句是表达式则额外求值返回（REPL 语义）。"""
    tree = ast.parse(src)
    val = None
    body = list(tree.body)
    if body and isinstance(body[-1], ast.Expr):
        last = ast.Expression(body[-1].value)
        rest = ast.Module(body=body[:-1], type_ignores=[])
        if rest.body:
            exec(compile(rest, "<bridge>", "exec"), g)
        val = eval(compile(last, "<bridge>", "eval"), g)
    else:
        exec(compile(tree, "<bridge>", "exec"), g)
    return val


def handle(req):
    op = req.get("op")

    if op == "eval":
        code = req.get("code", "")
        val = eval(compile(code, "<bridge>", "eval"), GLOBALS)
        return {"value": _jsonable(val)}

    if op == "exec":
        g = dict(GLOBALS)  # 每次新命名空间，但全局函数/np 可见；ds 注入本命名空间
        cols = {}
        for name, meta in (req.get("cols") or {}).items():
            cols[str(name)] = _load_col(meta)
        ds = Ds(cols)
        g["ds"] = ds
        out_buf = io.StringIO()
        with contextlib.redirect_stdout(out_buf):
            val = _run_source(req.get("source", ""), g)
        # 收集写回列 -> f64 文件旁路（np.asarray 保证 C 连续 <f8）
        out_dir = req.get("out_dir") or ""
        new = {}
        for name, arr in ds._new.items():
            arr = np.ascontiguousarray(arr, dtype="<f8")
            path = out_dir + "/pynew-" + str(abs(hash(name))) + ".bin"
            arr.tofile(path)
            new[name] = {"path": path, "rows": int(arr.shape[0]) if arr.ndim else 0}
        return {"value": _jsonable(val), "out": out_buf.getvalue(), "new": new}

    if op == "put":
        GLOBALS[str(req["name"])] = _load_col(req)
        return {}

    if op == "pull":
        arr = np.ascontiguousarray(np.asarray(GLOBALS[req["name"]], dtype="<f8"))
        arr.tofile(req["path"])
        return {"rows": int(arr.shape[0] if arr.ndim else 0)}

    raise ValueError(f"未知 op: {op!r}")


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except Exception as e:  # 协议坏了也要回一条，避免宿主死等
            sys.stdout.write(json.dumps({"id": None, "ok": False, "error": f"bad json line: {e}"}) + "\n")
            sys.stdout.flush()
            continue
        rid = req.get("id")
        try:
            resp = {"id": rid, "ok": True}
            resp.update(handle(req))
        except Exception as e:
            resp = {"id": rid, "ok": False, "error": f"{type(e).__name__}: {e}"}
        sys.stdout.write(json.dumps(resp, ensure_ascii=False) + "\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
