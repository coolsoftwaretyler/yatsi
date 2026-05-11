// Basic for loop
for (let i: number = 0; i < 5; i = i + 1) {
  console.log(i);
}

// For loop with no init (use existing var)
let j: number = 10;
for (; j > 7; j = j - 1) {
  console.log(j);
}

// For loop computing sum
let sum: number = 0;
for (let k: number = 1; k <= 4; k = k + 1) {
  sum = sum + k;
}
console.log(sum);