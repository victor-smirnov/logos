fn main() { let a = [1, 2]; let r: Vec<&i32> = vec![&a[0], &a[1]]; println!("{:?}", r); }
