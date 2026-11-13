//! PlotScope 插件 ABI 的 Rust 镜像。
//! 只描述 `ps_plugin_api.h` 的 C 布局；不依赖 ps-ndf / ps-abi，插件不会静态链接第二份 kernel。

use std::ffi::c_void;
use std::os::raw::c_char;

pub const PS_PLUGIN_ABI_VERSION: u32 = 4;
pub const PS_HOST_API_VERSION: u32 = 1;
pub const PS_FAMILY_DATA_LOADER: u32 = 1;
pub const PS_FAMILY_DATA_SOURCE: u32 = 2;
pub const PS_FAMILY_PARSER: u32 = 3;
pub const PS_FAMILY_TOOLBOX: u32 = 4;
pub const PS_FAMILY_PANEL: u32 = 5;

pub const PS_CAP_FILE: u32 = 1 << 0;
pub const PS_CAP_PROBE: u32 = 1 << 1;
pub const PS_CAP_PARAMS: u32 = 1 << 2;
pub const PS_CAP_SCHEMA: u32 = 1 << 3;
pub const PS_CAP_UI: u32 = 1 << 4;
pub const PS_CAP_CACHE: u32 = 1 << 5;
pub const PS_CAP_STREAM: u32 = 1 << 6;
pub const PS_CAP_RECORD: u32 = 1 << 7;
pub const PS_CAP_COMMAND: u32 = 1 << 8;

pub type DatasetNewFn = unsafe extern "C" fn() -> *mut c_void;
pub type DatasetAddChannelFn = unsafe extern "C" fn(
    dataset: *mut c_void,
    name: *const c_char,
    unit: *const c_char,
    time_s: *const f64,
    value: *const f64,
    count: u32,
    err: *mut c_char,
    err_cap: u32,
) -> i32;
pub type DatasetFreeFn = unsafe extern "C" fn(dataset: *mut c_void);

#[repr(C)]
pub struct PsHostApi {
    pub abi_version: u32,
    pub kernel_abi_version: u32,
    pub dataset_new: Option<DatasetNewFn>,
    pub dataset_add_channel: Option<DatasetAddChannelFn>,
    pub dataset_free: Option<DatasetFreeFn>,
}

pub type SchemaJsonFn = unsafe extern "C" fn(out: *mut c_char, cap: u32) -> i32;
pub type ProbeJsonFn =
    unsafe extern "C" fn(path: *const c_char, opts_json: *const c_char, out: *mut c_char, cap: u32) -> i32;
pub type DefaultOptionsFn =
    unsafe extern "C" fn(path: *const c_char, out: *mut c_char, cap: u32) -> i32;
pub type ParamsJsonFn = unsafe extern "C" fn(path: *const c_char, out: *mut c_char, cap: u32) -> i32;
pub type LoadFn = unsafe extern "C" fn(
    path: *const c_char,
    opts_json: *const c_char,
    host: *const PsHostApi,
    dataset: *mut *mut c_void,
    err: *mut c_char,
    err_cap: u32,
) -> i32;

#[repr(C)]
pub struct PsLoaderVtable {
    pub abi_version: u32,
    pub id: *const c_char,
    pub label: *const c_char,
    pub filter: *const c_char,
    pub extensions: *const c_char,
    pub format_tag: *const c_char,
    pub tree: u8,
    pub has_params: u8,
    pub reserved: [u8; 6],
    pub schema_json: Option<SchemaJsonFn>,
    pub probe_json: Option<ProbeJsonFn>,
    pub default_options: Option<DefaultOptionsFn>,
    pub params_json: Option<ParamsJsonFn>,
    pub load: Option<LoadFn>,
}

#[repr(C)]
pub struct PsUiVtable {
    pub abi_version: u32,
    pub dialog_ui: *const c_char,
    pub panels_json: *const c_char,
    pub ribbon_json: *const c_char,
    pub get_data: Option<unsafe extern "C" fn(*const c_char, *mut c_char, u32) -> i32>,
    pub on_event:
        Option<unsafe extern "C" fn(*const c_char, *const c_char, *mut c_char, u32) -> i32>,
    pub on_tick: Option<unsafe extern "C" fn(*const c_char, *mut c_char, u32) -> i32>,
}

#[repr(C)]
pub struct PsSourceSink {
    pub ctx: *mut c_void,
    pub channel_batch: Option<
        unsafe extern "C" fn(*mut c_void, *const c_char, *const c_char, *const f64, *const f64, u32) -> i32,
    >,
    pub event: Option<unsafe extern "C" fn(*mut c_void, *const c_char, *const c_char) -> i32>,
}

pub type OpenFn = unsafe extern "C" fn(
    config_json: *const c_char,
    sink: *const PsSourceSink,
    host: *const PsHostApi,
    handle: *mut *mut c_void,
    err: *mut c_char,
    err_cap: u32,
) -> i32;

#[repr(C)]
pub struct PsDataSourceVtable {
    pub abi_version: u32,
    pub id: *const c_char,
    pub label: *const c_char,
    pub kind: *const c_char,
    pub transport: *const c_char,
    pub recordable: u8,
    pub reserved: [u8; 7],
    pub config_schema_json: Option<SchemaJsonFn>,
    pub default_config_json: Option<SchemaJsonFn>,
    pub open: Option<OpenFn>,
    pub poll: Option<unsafe extern "C" fn(*mut c_void, i32, *mut c_char, u32) -> i32>,
    pub close: Option<unsafe extern "C" fn(*mut c_void) -> i32>,
    pub status_json: Option<unsafe extern "C" fn(*mut c_void, *mut c_char, u32) -> i32>,
    pub send_command:
        Option<unsafe extern "C" fn(*mut c_void, *const c_char, *mut c_char, u32) -> i32>,
}

#[repr(C)]
pub struct PsPluginDescriptor {
    pub abi_version: u32,
    pub manifest_json: *const c_char,
    pub family: u32,
    pub capabilities: u32,
    pub loader: *const PsLoaderVtable,
    pub ui: *const PsUiVtable,
    pub source: *const PsDataSourceVtable,
}

pub type PsPluginEntryFn = unsafe extern "C" fn() -> *const PsPluginDescriptor;

unsafe impl Sync for PsLoaderVtable {}
unsafe impl Sync for PsPluginDescriptor {}
unsafe impl Sync for PsUiVtable {}
unsafe impl Sync for PsDataSourceVtable {}

/// 把错误写进宿主提供的缓冲区（不 panic 穿越 FFI）。
pub fn write_err(err: *mut c_char, cap: u32, msg: &str) {
    if err.is_null() || cap == 0 {
        return;
    }
    let bytes = msg.as_bytes();
    let n = bytes.len().min(cap as usize - 1);
    unsafe {
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), err as *mut u8, n);
        *err.add(n) = 0;
    }
}

pub fn write_bytes(out: *mut c_char, cap: u32, data: &[u8]) -> i32 {
    if out.is_null() || cap as usize <= data.len() {
        return -1;
    }
    unsafe {
        std::ptr::copy_nonoverlapping(data.as_ptr(), out as *mut u8, data.len());
        *out.add(data.len()) = 0;
    }
    data.len() as i32
}
