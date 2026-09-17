fn main() { let mut v: i64 = 1; let p: *mut i64 = &mut v as *mut i64; let q: *const i64 = &v as *const i64;
    if p < q { std::process::exit(1); } }
