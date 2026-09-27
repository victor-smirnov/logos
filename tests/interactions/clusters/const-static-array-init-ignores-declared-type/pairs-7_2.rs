static LIMITS: [i64; 3] = [10, 20, 30];
static K: i64 = 5;
fn main() { let i = 2; println!("{} {} {}", LIMITS[0], LIMITS[i], K); let a = LIMITS; println!("{}", a[1]); }
