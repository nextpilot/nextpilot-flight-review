// 诊断用：打印一个 .ulg 的通道数与点数（不参与构建产物）。
// 用法：cargo run --example ulog_dump -- <file.ulg>
fn main() {
    let Some(p) = std::env::args().nth(1) else {
        eprintln!("usage: ulog_dump <file.ulg>");
        return;
    };
    let t0 = std::time::Instant::now();
    match ps_parse::ulog::parse_file(std::path::Path::new(&p)) {
        Ok(log) => {
            let rows: usize = log.channels.iter().map(|c| c.t.len()).max().unwrap_or(0);
            println!("文件 {}：{} 通道，最多 {} 行，解析 {:?}", p, log.channels.len(), rows, t0.elapsed());
            for c in log.channels.iter().take(6) {
                println!("  {} ({} 点)", c.name, c.t.len());
            }
            println!("参数 {} 个：", log.params.len());
            for (k,v) in log.params.iter().take(5) {
                println!("  {} = {}", k, v);
            }
            for n in log.notes.iter().take(3) {
                println!("  note: {}", n);
            }
        }
        Err(e) => println!("解析失败：{}", e),
    }
}
