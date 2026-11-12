//! 诊断：cargo run --example can_dump -- <file.asc|.blf>
fn main() {
    let path = std::env::args().nth(1).expect("用法: can_dump <file>");
    let buf = std::fs::read(&path).unwrap();
    let fmt = ps_parse::sniff(&buf[..buf.len().min(4096)]);
    println!("文件 {} → {:?}", path, fmt);
    let t0 = std::time::Instant::now();
    let log = match fmt {
        ps_parse::Format::Blf => ps_parse::blf::parse_bytes(&buf),
        ps_parse::Format::Asc => ps_parse::asc::parse_str(&String::from_utf8_lossy(&buf)),
        _ => {
            println!("不是 CAN 日志");
            return;
        }
    };
    match log {
        Ok(l) => {
            println!("{} 帧，解析 {:.1} ms", l.frames.len(), t0.elapsed().as_secs_f64() * 1000.0);
            for n in &l.notes {
                println!("  · {}", n);
            }
            let mut per: std::collections::BTreeMap<String, usize> = Default::default();
            for f in &l.frames {
                *per.entry(format!("CAN{}.{}", f.bus, f.id_text())).or_default() += 1;
            }
            for (k, v) in &per {
                println!("  # {} → {} 帧", k, v);
            }
            let series = ps_parse::can::frames_to_series(&l.frames);
            println!("{} 条曲线", series.len());
            for (name, t, v) in series.iter().take(6) {
                println!("  {}: {} 点，首值 {:?}", name, t.len(), v.first());
            }
        }
        Err(e) => println!("解析失败：{}", e),
    }
}
