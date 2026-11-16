//! Rust CSV loader：真实文件 I/O + 解析 + 通过宿主函数表写数据集。
//! 不链接 ps-ndf / ps-abi，证明 extension 侧 Rust 不需要第二份 kernel。

use ps_extension_sdk::*;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;

static SCHEMA: &str = r#"{"id":"data_loader_rustcsv","title":"加载 CSV（Rust）","fields":[
{"key":"delimiter","type":"enum","label":"分隔符","default":"auto","options":[
{"value":"auto","label":"自动探测"},{"value":",","label":"逗号  ,"},{"value":";","label":"分号  ;"},
{"value":"\t","label":"制表符  \\t"}]},
{"key":"has_header","type":"bool","label":"第一行是表头","default":true},
{"key":"skip_rows","type":"int","label":"跳过开头行","default":0,"min":0,"max":1000}]}"#;

static DEFAULTS: &str = r#"{"delimiter":"auto","has_header":true,"skip_rows":0}"#;
static MANIFEST: &[u8] = b"{\"id\":\"data_loader_rustcsv\",\"name\":\"CSV (Rust)\",\"version\":\"0.1.0\",\"sdk\":\"4\",\"family\":\"data_loader\",\"kind\":\"file\",\"entry\":\"ps_extension_entry_data_loader_rustcsv\"}\0";

fn cstr(s: &str) -> CString {
    CString::new(s).unwrap_or_default()
}

unsafe fn c_str(ptr: *const c_char) -> String {
    if ptr.is_null() {
        String::new()
    } else {
        CStr::from_ptr(ptr).to_string_lossy().into_owned()
    }
}

fn parse_rows(text: &str, delim: char, mut tick: impl FnMut(usize)) -> Vec<Vec<String>> {
    let mut rows = Vec::new();
    let mut row = Vec::new();
    let mut field = String::new();
    let mut quoted = false;
    let total = text.chars().count().max(1);
    let mut seen = 0usize;
    let mut chars = text.chars().peekable();
    while let Some(ch) = chars.next() {
        seen += 1;
        if seen % 4096 == 0 {
            tick(seen * 100 / total);
        }
        if quoted {
            if ch == '"' {
                if chars.peek() == Some(&'"') {
                    field.push('"');
                    chars.next();
                } else {
                    quoted = false;
                }
            } else {
                field.push(ch);
            }
        } else if ch == '"' {
            quoted = true;
        } else if ch == delim {
            row.push(field.trim().to_string());
            field.clear();
        } else if ch == '\n' {
            row.push(field.trim().to_string());
            field.clear();
            if row.iter().any(|s| !s.is_empty()) {
                rows.push(std::mem::take(&mut row));
            } else {
                row.clear();
            }
        } else if ch != '\r' {
            field.push(ch);
        }
    }
    row.push(field.trim().to_string());
    if row.iter().any(|s| !s.is_empty()) {
        rows.push(row);
    }
    rows
}

fn detect_delim(text: &str) -> char {
    let line = text.lines().find(|l| !l.trim().is_empty()).unwrap_or("");
    let comma = line.matches(',').count();
    let semi = line.matches(';').count();
    let tab = line.matches('\t').count();
    if tab >= comma && tab >= semi && tab > 0 {
        '\t'
    } else if semi > comma {
        ';'
    } else {
        ','
    }
}

unsafe extern "C" fn schema_json(out: *mut c_char, cap: u32) -> i32 {
    write_bytes(out, cap, SCHEMA.as_bytes())
}

unsafe extern "C" fn defaults(_path: *const c_char, out: *mut c_char, cap: u32) -> i32 {
    write_bytes(out, cap, DEFAULTS.as_bytes())
}

unsafe extern "C" fn probe(path: *const c_char, opts: *const c_char, out: *mut c_char, cap: u32) -> i32 {
    let p = c_str(path);
    let text = match std::fs::read_to_string(&p) {
        Ok(t) => t,
        Err(e) => return write_bytes(out, cap, format!("{{\"error\":\"{e}\"}}").as_bytes()),
    };
    let o: serde_json::Value = serde_json::from_str(&c_str(opts)).unwrap_or_default();
    let delim = match o.get("delimiter").and_then(|v| v.as_str()).unwrap_or("auto") {
        "auto" => detect_delim(&text),
        "\\t" => '\t',
        s => s.chars().next().unwrap_or(','),
    };
    let rows = parse_rows(&text, delim, |_| {});
    let col_count = rows.iter().map(|r| r.len()).max().unwrap_or(0);
    let mut columns = Vec::new();
    for i in 0..col_count {
        let name = rows.first().and_then(|r| r.get(i)).cloned().unwrap_or_else(|| format!("c{i}"));
        columns.push(serde_json::json!({"name":name,"kind":"number"}));
    }
    let preview: Vec<_> = rows.iter().take(9).collect();
    let j = serde_json::json!({
        "delimiter": delim.to_string(),
        "has_header": true,
        "column_count": col_count,
        "row_count": rows.len().saturating_sub(1),
        "columns": columns,
        "preview": preview
    });
    write_bytes(out, cap, j.to_string().as_bytes())
}

unsafe extern "C" fn load(
    path: *const c_char,
    opts: *const c_char,
    host: *const PsHostApi,
    dataset: *mut *mut std::ffi::c_void,
    err: *mut c_char,
    err_cap: u32,
) -> i32 {
    if host.is_null() || dataset.is_null() {
        write_err(err, err_cap, "宿主函数表为空");
        return 1;
    }
    let h = &*host;
    if h.abi_version != PS_HOST_API_VERSION || h.dataset_new.is_none() || h.dataset_add_channel.is_none() {
        write_err(err, err_cap, "宿主 API 不兼容");
        return 1;
    }
    let p = c_str(path);
    let text = match std::fs::read_to_string(&p) {
        Ok(t) => t,
        Err(e) => {
            write_err(err, err_cap, &format!("读取失败：{e}"));
            return 2;
        }
    };
    let o: serde_json::Value = serde_json::from_str(&c_str(opts)).unwrap_or_default();
    let delim = match o.get("delimiter").and_then(|v| v.as_str()).unwrap_or("auto") {
        "auto" => detect_delim(&text),
        "\\t" => '\t',
        s => s.chars().next().unwrap_or(','),
    };
    let has_header = o.get("has_header").and_then(|v| v.as_bool()).unwrap_or(true);
    let skip = o.get("skip_rows").and_then(|v| v.as_u64()).unwrap_or(0) as usize;
    let progress = h.progress;
    let rows = parse_rows(&text, delim, |p| {
        if let Some(f) = progress {
            unsafe { f(10 + (p as u64) * 60 / 100, 100) };
        }
    });
    if rows.len() <= skip {
        write_err(err, err_cap, "没有数据行");
        return 3;
    }
    let data = &rows[skip..];
    let headers: Vec<String> = if has_header {
        data[0].clone()
    } else {
        (0..data[0].len()).map(|i| format!("c{i}")).collect()
    };
    let start = if has_header { 1 } else { 0 };
    let ncol = headers.len();
    if ncol < 2 {
        write_err(err, err_cap, "没有可画的数值列");
        return 4;
    }
    let mut time = Vec::<f64>::new();
    let mut cols = vec![Vec::<f64>::new(); ncol - 1];
    for row in &data[start..] {
        let t = match row.first().and_then(|s| s.parse::<f64>().ok()) {
            Some(v) => v,
            None => continue,
        };
        let mut ok = true;
        let mut vals = Vec::with_capacity(ncol - 1);
        for i in 1..ncol {
            match row.get(i).and_then(|s| s.parse::<f64>().ok()) {
                Some(v) => vals.push(v),
                None => {
                    ok = false;
                    break;
                }
            }
        }
        if !ok {
            continue;
        }
        time.push(t);
        for (i, v) in vals.into_iter().enumerate() {
            cols[i].push(v);
        }
    }
    if time.is_empty() {
        write_err(err, err_cap, "没有可画的数值行");
        return 5;
    }
    let n = time.len();
    let ds = (h.dataset_new.unwrap())();
    for i in 0..ncol - 1 {
        let name = cstr(&headers[i + 1]);
        let unit = cstr("");
        let rc = (h.dataset_add_channel.unwrap())(
            ds,
            name.as_ptr(),
            unit.as_ptr(),
            time.as_ptr(),
            cols[i].as_ptr(),
            n as u32,
            err,
            err_cap,
        );
        if rc != 0 {
            if let Some(free) = h.dataset_free {
                free(ds);
            }
            return 6;
        }
    }
    *dataset = ds;
    if let Some(f) = progress {
        unsafe { f(100, 100) };
    }
    0
}

static LOADER: PsLoaderVtable = PsLoaderVtable {
    abi_version: PS_EXTENSION_ABI_VERSION,
    id: c"data_loader_rustcsv".as_ptr(),
    label: c"CSV (Rust)".as_ptr(),
    filter: c"Rust CSV (*.pscsv);;全部 (*)".as_ptr(),
    extensions: c".pscsv".as_ptr(),
    format_tag: c"CSV · Rust".as_ptr(),
    tree: 0,
    has_params: 0,
    reserved: [0; 6],
    schema_json: Some(schema_json),
    probe_json: Some(probe),
    default_options: Some(defaults),
    params_json: None,
    load: Some(load),
};

static EXTENSION: PsExtensionDescriptor = PsExtensionDescriptor {
    abi_version: PS_EXTENSION_ABI_VERSION,
    manifest_json: MANIFEST.as_ptr() as *const c_char,
    family: PS_FAMILY_DATA_LOADER,
    capabilities: PS_CAP_FILE | PS_CAP_PROBE | PS_CAP_SCHEMA | PS_CAP_CACHE,
    loader: &LOADER,
    ui: std::ptr::null(),
    source: std::ptr::null(),
    data_source: std::ptr::null(),
    protocol: std::ptr::null(),
};

#[no_mangle]
pub extern "C" fn ps_extension_entry_data_loader_rustcsv() -> *const PsExtensionDescriptor {
    &EXTENSION
}

#[no_mangle]
pub extern "C" fn ps_extension_entry() -> *const PsExtensionDescriptor {
    &EXTENSION
}
