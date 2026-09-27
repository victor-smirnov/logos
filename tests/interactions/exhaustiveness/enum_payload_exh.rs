// exhaustiveness oracle battery (ADR 0030 S3): Shape{Circle(r),Rect(w,h),Empty}
#![allow(dead_code, unused_variables, unused_assignments)]
enum Shape { Circle(i64), Rect(i64, i64), Empty }

fn run() -> i32 { let s = Shape::Rect(3i64, 4i64); let r: i64 = match s { Shape::Circle(x) => x, Shape::Rect(w, h) => w * h, Shape::Empty => 0i64 }; return r as i32; }

fn main() { std::process::exit(run()); }
