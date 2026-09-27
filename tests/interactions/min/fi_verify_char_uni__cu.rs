fn main() {
    let c = 'é';
    println!("{} {} {} {}", c.is_alphabetic(), c.is_alphanumeric(), c.is_uppercase(), c.is_lowercase());
    println!("{}", 'Ж'.is_uppercase());
}
