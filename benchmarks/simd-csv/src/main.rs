use simd_csv::{SplitterBuilder, ZeroCopyReaderBuilder};
use std::fs::File;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let file = File::open(&args[2]).expect("open");
    if args[1] == "count" {
        let rows = SplitterBuilder::new()
            .has_headers(false)
            .from_reader(file)
            .count_records()
            .expect("parse error");
        println!("rows {rows}");
        return;
    }
    let name = args.get(3).map_or(&b""[..], |s| s.as_bytes());
    let mut reader = ZeroCopyReaderBuilder::new()
        .has_headers(false)
        .flexible(true)
        .from_reader(file);
    let (mut rows, mut fields, mut bytes, mut matches) = (0u64, 0u64, 0u64, 0u64);
    while let Some(record) = reader.read_byte_record().expect("parse error") {
        rows += 1;
        for cell in record.unescaped_iter() {
            fields += 1;
            bytes += cell.len() as u64;
            matches += (*cell == *name) as u64;
        }
    }
    if args[1] == "scan" {
        println!("rows {rows} fields {fields} bytes {bytes}");
    } else {
        println!("matches {matches}");
    }
}
