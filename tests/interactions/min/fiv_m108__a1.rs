fn main() {
    let grid = [[1i64, 2, 3, 4], [5i64, 6, 7, 8]];
    for row in grid.iter() { println!("{}", row[2]); }
}
