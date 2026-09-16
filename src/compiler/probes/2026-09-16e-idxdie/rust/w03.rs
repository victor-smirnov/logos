fn main(){let mut v:Vec<i64>=Vec::new();v.push(1);v.push(2);let e:&i64=&v[1usize];v[0usize]=*e+1;std::process::exit(v[0usize] as i32);}
