fn main() { for i in 0..4 { let g = format!("s{}", i); if i == 2 { let m = g; println!("moved {}", m); continue; } if i == 3 { drop(g); println!("explicit"); break; } } println!("end"); }
