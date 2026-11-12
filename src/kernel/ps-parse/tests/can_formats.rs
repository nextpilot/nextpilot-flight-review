//! ASC / BLF 双格式一致性测试：同一批帧用两种格式写出，解析结果必须逐点相同。
//! 夹具由 python-can（4.6.1）的 ASCWriter / BLFWriter 生成，等于拿第三方实现当裁判。

use ps_parse::can::frames_to_series;

fn fixture(name: &str) -> std::path::PathBuf {
    std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../../test/fixture")
        .join(name)
}

#[test]
fn asc_and_blf_agree_point_by_point() {
    let asc = ps_parse::asc::parse_file(&fixture("demo_can.asc")).expect("ASC 解析失败");
    let blf = ps_parse::blf::parse_file(&fixture("demo_can.blf")).expect("BLF 解析失败");

    // python-can 读同一对文件都是 3020 帧 / 5 个 ID
    assert_eq!(asc.frames.len(), 3020, "ASC 帧数应与 python-can 一致");
    assert_eq!(blf.frames.len(), 3020, "BLF 帧数应与 python-can 一致");

    let sa = frames_to_series(&asc.frames);
    let sb = frames_to_series(&blf.frames);
    assert_eq!(sa.len(), sb.len(), "两种格式应产出同一条数的曲线");
    for (a, b) in sa.iter().zip(sb.iter()) {
        assert_eq!(a.0, b.0, "通道名不一致");
        assert_eq!(a.1.len(), b.1.len(), "{} 点数不一致", a.0);
        for (x, y) in a.2.iter().zip(b.2.iter()) {
            assert!((x - y).abs() < 1e-9, "{} 数值不一致", a.0);
        }
    }
}
