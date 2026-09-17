fn main() { let mut v: i64 = 1; let p: *mut i64 = &mut v as *mut i64; let r: &i64 = &v;
    if p < r { std::process::exit(1); } }
