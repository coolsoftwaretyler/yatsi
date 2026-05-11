// && short-circuit: falsy left, right not evaluated
console.log(0 && 42);
console.log(false && "hello");

// && when left is truthy, returns right
console.log(1 && 42);
console.log("hi" && "there");

// || short-circuit: truthy left, right not evaluated
console.log(1 || 42);
console.log("hi" || "there");

// || when left is falsy, returns right
console.log(0 || 42);
console.log(false || "fallback");

// Chained
console.log(0 || 0 || 3);
console.log(1 && 2 && 3);
console.log(1 && 0 && 3);

// null/undefined behavior
console.log(null || "default");
console.log(undefined || "defined");