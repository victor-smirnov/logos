fn main() { let mut v: i64 = 1; let mut w: u8 = 2; let p: *mut i64 = &mut v as *mut i64; let q: *mut u8 = &mut w as *mut u8;
    if p < q { std::process::exit(1); } }
