fn main() { let mut v: i64 = 1; let p: *mut i64 = &mut v as *mut i64;
    if p < 0 { std::process::exit(1); } }
