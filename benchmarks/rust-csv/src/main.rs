use csv::{ByteRecord, ReaderBuilder};

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut reader = ReaderBuilder::new()
        .has_headers(false)
        .flexible(true)
        .from_path(&args[2])
        .expect("open");
    let name = args.get(3).map_or(&b""[..], |s| s.as_bytes());
    let mut record = ByteRecord::new();
    let (mut rows, mut fields, mut bytes, mut matches) = (0u64, 0u64, 0u64, 0u64);
    while reader.read_byte_record(&mut record).expect("parse error") {
        rows += 1;
        for cell in record.iter() {
            fields += 1;
            bytes += cell.len() as u64;
            matches += (cell == name) as u64;
        }
    }
    match args[1].as_str() {
        "count" => println!("rows {rows}"),
        "scan" => println!("rows {rows} fields {fields} bytes {bytes}"),
        _ => println!("matches {matches}"),
    }
}
