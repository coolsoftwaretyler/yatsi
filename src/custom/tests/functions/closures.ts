function makeCounter() {
    let count = 0;
    return () => {
        count = count + 1;
        return count;
    };
}

let counter = makeCounter();
console.log(counter());
console.log(counter());
console.log(counter());