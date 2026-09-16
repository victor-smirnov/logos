fn main(){let mut v:Vec<i64>=Vec::new();v.push(5);v.push(1);let e:&i64=&v[1usize];v[*e as usize]=*e+6;std::process::exit(v[1usize] as i32);}
