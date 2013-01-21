function f(i) { return [i]; }
function g(x) { return x[0]++; }
var p = new ParallelArray(1000, f);
print(p);
