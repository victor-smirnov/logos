fn main() { let mut x = 1i32; let p: *mut i32 = &mut x; let set = |v: i32| unsafe { *p = v; }; set(4); set(7); println!("{}", x); }
