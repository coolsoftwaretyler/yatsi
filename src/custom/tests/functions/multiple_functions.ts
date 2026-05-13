function square(x: number): number {
    return x * x;
}
function cube(x: number): number {
    return x * square(x);
}
console.log(square(3));
console.log(cube(3));