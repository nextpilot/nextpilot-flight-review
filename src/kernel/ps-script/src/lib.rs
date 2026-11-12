//! Lua 5.4 脚本宿主（D5）。M0 只有预算与沙箱配置，语言绑定在 M2 接。
//! 约定：脚本只做编排，数学在 ps-ops 的向量核里跑，禁止逐点循环。

#[derive(Debug, Clone)]
pub struct Budget {
    /// 单块处理的时间预算（毫秒）。超预算记一次告警并跳过该块，不阻塞 UI。
    pub block_ms: u32,
    /// 单脚本内存上限（字节）。
    pub mem_bytes: usize,
    /// 单次调用最多触碰的样本数。
    pub max_samples: usize,
}

impl Default for Budget {
    fn default() -> Self {
        Self { block_ms: 10, mem_bytes: 16 * 1024 * 1024, max_samples: 1_000_000 }
    }
}

/// 脚本执行态：M0 仅占位，记录当前装载的脚本名与预算。
#[derive(Debug, Default)]
pub struct ScriptState {
    pub name: String,
    pub budget: Budget,
    pub loaded: bool,
}

impl ScriptState {
    pub fn load(name: impl Into<String>) -> Self {
        Self { name: name.into(), budget: Budget::default(), loaded: true }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_budget_is_10ms_block() {
        assert_eq!(Budget::default().block_ms, 10);
    }

    #[test]
    fn load_marks_loaded() {
        assert!(ScriptState::load("yaw_rate.lua").loaded);
    }
}
