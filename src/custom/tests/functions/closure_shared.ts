function makeAdder(x: number) {
    return (y: number): number => x + y;
}

let add5 = makeAdder(5);
let add10 = makeAdder(10);
console.log(add5(3));
console.log(add10(3));
console.log(add5(100));