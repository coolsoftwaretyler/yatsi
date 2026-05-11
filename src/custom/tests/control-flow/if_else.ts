// Basic if
let x: number = 10;
if (x > 5) {
  console.log("greater");
}

// if/else
let y: number = 3;
if (y > 5) {
  console.log("big");
} else {
  console.log("small");
}

// Truthiness: 0 is falsy
let z: number = 0;
if (z) {
  console.log("truthy");
} else {
  console.log("falsy");
}

// Truthiness: non-zero is truthy
if (1) {
  console.log("one is truthy");
}

// Nested if/else
let a: number = 15;
if (a > 20) {
  console.log("above 20");
} else if (a > 10) {
  console.log("above 10");
} else {
  console.log("10 or below");
}

// String truthiness: empty string is falsy
let s: string = "";
if (s) {
  console.log("has string");
} else {
  console.log("empty string");
}