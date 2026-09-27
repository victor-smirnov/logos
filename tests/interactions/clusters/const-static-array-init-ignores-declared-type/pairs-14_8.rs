const N: usize = 4;
const TABLE: [i64; N] = [10, 20, 30, 40];
const T2: [i64; 3] = [1, 2, 3];
fn main() { println!("{} {}", TABLE[1], T2[2]); let t = TABLE; println!("{}", t[3]); }
