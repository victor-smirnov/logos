fn main() {
    let mut count = 0;
    let mut inc_if = |v: i32| if v > 1 { count += 1; };
    inc_if(2); inc_if(0);
    let f = |x: i32| if x > 0 { println!("pos") };
    f(1);
    println!("{}", count);
}
