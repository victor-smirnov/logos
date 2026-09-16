fn main(){let mut a:[i64;2]=[1,0];let e:&i64=&a[1usize];a[*e as usize]=9;std::process::exit(a[0usize] as i32);}
