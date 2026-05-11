// Basic while loop
let i: number = 0;
while (i < 5) {
  console.log(i);
  i = i + 1;
}

// While with falsy condition (never enters)
let count: number = 0;
while (count > 10) {
  console.log("should not print");
  count = count + 1;
}
console.log("after empty while");

// Countdown
let n: number = 3;
while (n > 0) {
  console.log(n);
  n = n - 1;
}
console.log("done");