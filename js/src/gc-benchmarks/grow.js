// Adapted from grow.sch (by Lars Hansen for the Larceny runtime).
//
// The benchmark builds 25 sequences of length 100,000, all of which become
// garbage after having been built up.
//
// Lars has further Larceny-specific notes in the original grow.sch source.

load(libdir + "util.js");

function int_sequence_benchmark(iters) {
  return run_benchmark("int-sequence",
                       () => sequence_run(build_sequence_of_ints),
                       iters ? iters : 1);
}

function list_sequence_benchmark(iters) {
  return run_benchmark("list-sequence",
                       () => sequence_run(build_sequence_of_lists),
                       iters ? iters : 1);
}

function sequence_run(p) {
  for (var i=0; i < 25; i++) {
    p();
  }
}

// Builds a sequence of 100,000 ints and then drops it on the floor.
function build_sequence_of_ints() {
  var s = make_sequence();
  for (var i=0; i < 100000; i++) {
    sequence_addhi(s, i);
  }
}

// Builds a sequence of 100,000 lists of int of length 1, and then drops it
// on the floor.
function build_sequence_of_lists() {
  var s = make_sequence();
  for (var i=0; i < 100000; i++) {
    sequence_addhi(s, cons(i, null));
  }
}

// A sequence is a growable array.
//
// It's represented as a pair: the car is the next index to use, the cdr 
// is the current vector.  Every time an element is added at the end and 
// there is no room, the vector's length is doubled (this is what the 
// Modula-3 implementation does).

var seq_default_size = 100;

function make_sequence() {
    return cons(0, make_vector(seq_default_size, 0));
}

function sequence_addhi(s, x) {
  var next = s.car;
  var v    = s.cdr;
  if (next === v.length) {
    var w = make_vector(v.length * 2, 0);
    for (var i = v.length - 1; i >= 0; i--) {
      w[i] = v[i];
    }
    s.cdr = w;
    sequence_addhi(s, x);
  } else {
    v[next] = x;
    s.car = next + 1;
  }
}

print(JSON.stringify(int_sequence_benchmark(10)));
print(JSON.stringify(list_sequence_benchmark(10)));

// eof
