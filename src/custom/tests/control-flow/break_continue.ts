// Break in while loop
let i: number = 0;
while (i < 10) {
  if (i == 3) {
    break;
  }
  console.log(i);
  i = i + 1;
}
console.log("after break");

// Continue in while loop
let j: number = 0;
while (j < 5) {
  j = j + 1;
  if (j == 3) {
    continue;
  }
  console.log(j);
}
console.log("after continue");

// Break in for loop
for (let k: number = 0; k < 10; k = k + 1) {
  if (k == 4) {
    break;
  }
  console.log(k);
}
console.log("after for break");

// Continue in for loop (must jump to update, not condition)
for (let m: number = 0; m < 5; m = m + 1) {
  if (m == 2) {
    continue;
  }
  console.log(m);
}
console.log("after for continue");