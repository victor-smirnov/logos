struct N { val: i64 }
fn main() {
    let r = loop { let b = Box::new(N { val: 40 }); if b.val > 30 { break b.val; } };
    let s = loop { let b = N { val: 41 }; if b.val > 30 { break b.val; } };
    println!("{} {}", r, s);
}
