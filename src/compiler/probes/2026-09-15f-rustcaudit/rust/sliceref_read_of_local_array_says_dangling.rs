fn f<'a>(x: &'a i64, y: &'a i64) -> &'a i64 {
  let mut out: [&'a i64; 1] = [x];
  out[0usize] = y;
  let s: &[&'a i64] = &out;
  return s[0usize];
}
fn logos_main() -> i32 { return 0i32; }

fn main() { std::process::exit(logos_main()); }
