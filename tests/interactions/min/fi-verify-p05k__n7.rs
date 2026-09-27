fn main() { let c = &|| 7i64; let r = c(); if r == 7 { return; } std::process::exit(1); }
