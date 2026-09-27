fn g1(n: i64) -> Option<i64> { let mut i: i64 = 0; let r = loop { if i >= n { break None; } if i == 3 { break Some(i); } i += 1; }; r }
fn g2(n: i64) -> Option<i64> { let mut i: i64 = 0; let r: Option<i64> = loop { if i >= n { break None; } if i == 3 { break Some(i); } i += 1; }; r }
fn g3(n: i64) -> Option<i64> { let mut i: i64 = 0; let r = loop { if i == 3 { break Some(i); } if i >= n { break None; } i += 1; }; r }
fn g4(n: i64) -> Option<i64> { let mut i: i64 = 0; loop { if i >= n { break None; } if i == 3 { break Some(i); } i += 1; } }
fn main() {
    println!("{:?} {:?} {:?} {:?}", g1(10), g2(10), g3(10), g4(10));
}
