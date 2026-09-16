fn use_sl(s: &[i64]) -> i64 { s.len() as i64 }
fn main() {
    let b: Box<[i64]> = vec![1i64, 2, 3].into_boxed_slice();
    assert_eq!(use_sl(&b), 3);
}
