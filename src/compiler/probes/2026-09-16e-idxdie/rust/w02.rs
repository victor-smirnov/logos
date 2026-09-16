fn main(){let mut v:Vec<i64>=Vec::new();v.push(1);v.push(0);let e:&i64=&v[1usize];let i=*e as usize;v[i]=9;std::process::exit(v[0usize] as i32);}
