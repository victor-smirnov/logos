fn main() {
    let v: String = 'a: loop { let s = String::from("x"); break 'a format!("{}-{}", s, s); };
    let w: String = loop { let s = String::from("y"); break format!("{}-{}", s, s); };
    println!("{} {}", v, w);
}
