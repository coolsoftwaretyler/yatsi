// Nested for loops (multiplication table fragment)
for (let i: number = 1; i <= 3; i = i + 1) {
  for (let j: number = 1; j <= 3; j = j + 1) {
    console.log(i * j);
  }
}

// Nested while with break (only breaks inner)
let outer: number = 0;
while (outer < 3) {
  let inner: number = 0;
  while (inner < 5) {
    if (inner == 2) {
      break;
    }
    console.log(outer, inner);
    inner = inner + 1;
  }
  outer = outer + 1;
}

// For with nested if and continue
for (let x: number = 0; x < 4; x = x + 1) {
  for (let y: number = 0; y < 4; y = y + 1) {
    if ((x + y) % 2 == 0) {
      continue;
    }
    console.log(x, y);
  }
}