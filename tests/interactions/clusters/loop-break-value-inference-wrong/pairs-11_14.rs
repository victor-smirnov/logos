fn main() {
    let i = 3;
    let g: Option<i32> = loop { if i > 100 { break None; } break Some(7); };
    let h = loop { if i > 100 { break None; } break Some(8); };
    let k: Option<i32> = h;
    println!("{:?} {:?}", g, k);
}
