function plus(x,y) { return x + y; }

function Pair(a,d) { this.car = a; this.cdr = d; }
function cons(a,d) { return new Pair(a,d); }
function car(x) { return x.car; }
function cdr(x) { return x.cdr; }

function reverse_inplace(lst) {
  if (lst === null)
    return lst;
  else
    return rotate(lst, null);

  function rotate(fo, fum) {
    while (true) {
      var next = fo.cdr;
      fo.cdr = fum;
      if (next === null)
        return fo;
      fum = fo;
      fo = next;
    }
  }
}
function map(f, l) {
  var rev = null;
  while (l !== null) {
    rev = cons(f(l.car), rev);
    l = l.cdr;
  }
  return reverse_inplace(rev);
}
function memq(x, l) {
  while (l !== null) {
    if (l.car === x)
      return true;
    l = l.cdr;
  }
  return false;
}
function apply(f, l) {
  var args = [];
  while (l !== null) {
    args.push(l.car);
    l = l.cdr;
  }
  return f.apply(null, args);
}
function append(...lists) {
  function concat(x,y) {
    while (true) {
      if (x === null)
        return y;
      y = cons(x.car, y);
      x = x.cdr;
    }
  }
  var accum = null;
  while (lists.length > 0) {
    var l = lists.shift();
    accum = concat(accum, l);
  }
  return accum;
}

function make_vector(len, x) {
  var v = new Array(len);
  for (var i = 0; i < len; i++) { v[i] = x; }
  return v;
}

function run_benchmark(name, f, iters) {
  var start = new Date();
  for (var i=0; i < iters; i++) {
    var result = f();
  }
  var end = new Date();
  return [end.getTime() - start.getTime(), result];
}
