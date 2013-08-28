DEFAULT_WARMUP = 10
DEFAULT_MEASURE = 3
MODE = MODE || "compare" // MODE is often set on the command-line by run.sh

/**
 * label: for the printouts
 * w: warmup runs
 * m: measurement runs
 * seq: closure to compute sequentially
 * par: closure to compute in parallel
 */
function benchmark(label, w, m, seq, par) {
  var SEQ = 1
  var PAR = 2
  var bits = 0
  if (MODE === "seq" || MODE === "one") { bits = SEQ; }
  else if (MODE === "par" || MODE === "two") { bits = PAR; }
  else {
    if (MODE !== "compare") {
      print("Invalid MODE, expected seq|par|compare: ", MODE);
    }
    bits = SEQ|PAR;
  }
  var seq_params = new BenchParams(SEQ, "sequential", "SEQ", w, m, seq);
  var par_params = new BenchParams(PAR, "parallel", "PAR", w, m, par);
  benchmark_generic(bits, label, seq_params, par_params);
}

function BenchParams(bit, label, tag, w, m, thunk) {
  this.bit = bit;
  this.label = label;
  this.tag = tag;
  this.warmups = w;
  this.measurements = m;
  this.thunk = thunk;
}

function benchmark_pair(label, w, m, one_label, one_f, two_label, two_f) {
  var ONE = 1;
  var TWO = 2;
  var bits = 0;
  if (MODE === "one") { bits = ONE; }
  else if (MODE === "two") { bits = TWO; }
  else {
    if (MODE !== "compare") {
      print("Invalid MODE, expected one|two|compare: ", MODE);
    }
    bits = ONE|TWO;
  }
  var one_params =
    new BenchParams(ONE, one_label, one_label.slice(0,3).toUpperCase(), w, m, one_f);
  var two_params =
    new BenchParams(TWO, two_label, two_label.slice(0,3).toUpperCase(), w, m, two_f);
  benchmark_generic(bits, label, one_params, two_params);
}

function benchmark_generic(bits, label, seq, par) {
  if (mode(seq.bit)) {
    print("Warming up "+seq.label+" runs");
    warmup(seq.warmups, seq.thunk);

    print("Measuring "+seq.label+" runs");
    var [seqTimes, seqResult] = measureN(seq.measurements, seq.thunk);
  }

  if (mode(par.bit)) {
    print("Warming up "+par.label+" runs");
    warmup(par.warmups, par.thunk);

    print("Measuring "+par.label+" runs");
    var [parTimes, parResult] = measureN(par.measurements, par.thunk);
  }

  if (mode(seq.bit|par.bit)) {
    // Check correctness
    print("Checking correctness");
    assertStructuralEq(seqResult, parResult);
  }

  var SEQ_LABEL = seq.label.toUpperCase();
  var PAR_LABEL = par.label.toUpperCase();

  if (mode(seq.bit)) {
    var seqAvg = average(seqTimes);
    for (var i = 0; i < seqTimes.length; i++)
      print(label + " "+SEQ_LABEL+" MEASUREMENT " + i + ": " + seqTimes[i]);
    print(label + " "+SEQ_LABEL+" AVERAGE: " + seqAvg);
  }

  if (mode(par.bit)) {
    var parAvg = average(parTimes);
    for (var i = 0; i < parTimes.length; i++)
      print(label + " "+PAR_LABEL+" MEASUREMENT " + i + ": " + parTimes[i]);
    print(label + " "+PAR_LABEL+" AVERAGE  : " + parAvg);
  }

  if (mode(seq.bit|par.bit)) {
    print(label + " "+seq.tag+"/"+par.tag+" RATIO     : " + seqAvg/parAvg);
    print(label + " "+par.tag+"/"+seq.tag+" RATIO     : " + parAvg/seqAvg);
    print(label + " IMPROVEMENT       : " +
          (((seqAvg - parAvg) / seqAvg * 100.0) | 0) + "%");
  }

  function mode(m) {
    return (bits & m) === m;
  }
}

function measure1(f) {
  var start = new Date();
  result = f();
  var end = new Date();
  return [end.getTime() - start.getTime(), result];
}

function warmup(iters, f) {
  for (var i = 0; i < iters; i++) {
    print(".");
    f();
  }
}

function average(measurements) {
  var sum = measurements.reduce(function (x, y) { return x + y; });
  return sum / measurements.length;
}

function measureN(iters, f) {
  var measurement, measurements = [];
  var result;

  for (var i = 0; i < iters; i++) {
    [measurement, result] = measure1(f);
    measurements.push(measurement);
  }

  return [measurements, result];
}

function isParArrOrMat(e) {
  return (e instanceof ParallelArray) || (e instanceof Matrix);
}

function isTypedArr(e) {
  return e instanceof Uint8Array ||
    e instanceof Uint8ClampedArray ||
    e instanceof Uint16Array ||
    e instanceof Uint32Array ||
    e instanceof Int8Array ||
    e instanceof Int16Array ||
    e instanceof Int32Array ||
    e instanceof Float32Array ||
    e instanceof Float64Array;
}

function assertStructuralEq(e1, e2) {
    if (isParArrOrMat(e1) && isParArrOrMat(e2)) {
      assertEqParallelArray(e1, e2);
    } else if (e1 instanceof Array && isParArrOrMat(e2)) {
      assertEqParallelArrayArray(e2, e1);
    } else if (typeof(RectArray) != "undefined" &&
               e1 instanceof ParallelArray && e2 instanceof RectArray) {
      assertEqParallelArrayRectArray(e1, e2);
    } else if (typeof(RectArray) != "undefined" &&
               e1 instanceof RectArray && e2 instanceof ParallelArray) {
      assertEqParallelArrayRectArray(e2, e1);
    } else if (typeof(WrapArray) != "undefined" &&
               e1 instanceof ParallelArray && e2 instanceof WrapArray) {
      assertEqParallelArrayWrapArray(e1, e2);
    } else if (typeof(WrapArray) != "undefined" &&
               e1 instanceof WrapArray && e2 instanceof ParallelArray) {
      assertEqParallelArrayWrapArray(e2, e1);
    } else if (e1 instanceof Array && e2 instanceof ParallelArray) {
      assertEqParallelArrayArray(e2, e1);
    } else if (isTypedArr(e1) && isParArrOrMat(e2)) {
      assertEqParallelArrayArray(e2, e1);
    } else if (isParArrOrMat(e1) && e2 instanceof Array) {
      assertEqParallelArrayArray(e1, e2);
    } else if (isParArrOrMat(e1) && isTypedArr(e2)) {
      assertEqParallelArrayArray(e1, e2);
    } else if (typeof(RectArray) != "undefined" &&
               e1 instanceof RectArray && e2 instanceof RectArray) {
      assertEqRectArray(e1, e2);
    } else if (typeof(WrapArray) != "undefined" &&
               e1 instanceof WrapArray && e2 instanceof WrapArray) {
      assertEqWrapArray(e1, e2);
    } else if (e1 instanceof Array && e2 instanceof Array) {
      assertEqArray(e1, e2);
    } else if (e1 instanceof Object && e2 instanceof Object) {
      assertEq(e1.__proto__, e2.__proto__);
      for (prop in e1) {
        if (e1.hasOwnProperty(prop)) {
          assertEq(e2.hasOwnProperty(prop), true);
          assertStructuralEq(e1[prop], e2[prop]);
        }
      }
    } else {
      assertEq(e1, e2);
    }
}

function assertEqParallelArrayRectArray(a, b) {
  assertEq(a.shape.length, 2);
  assertEq(a.shape[0], b.width);
  assertEq(a.shape[1], b.height);
  for (var i = 0, w = a.shape[0]; i < w; i++) {
    for (var j = 0, h = a.shape[1]; j < h; j++) {
      assertStructuralEq(a.get(i,j), b.get(i,j));
    }
  }
}

function assertEqParallelArrayWrapArray(a, b) {
  assertEq(a.shape.length, 2);
  assertEq(a.shape[0], b.width);
  assertEq(a.shape[1], b.height);
  for (var i = 0, w = a.shape[0]; i < w; i++) {
    for (var j = 0, h = a.shape[1]; j < h; j++) {
      assertStructuralEq(a.get(i,j), b.get(i,j));
    }
  }
}

function assertEqParallelArrayArray(a, b) {
  assertEq(a.shape.length, 1);
  assertEq(a.shape[0], b.length); // Matrix does not have a .length property, even in 1D case; (perhaps it should).
  for (var i = 0, l = a.shape[0]; i < l; i++) {
    assertStructuralEq(a.get(i), b[i]);
  }
}

function assertEqRectArray(a, b) {
  assertEq(a.width, b.width);
  assertEq(a.height, b.height);
  for (var i = 0, w = a.width; i < w; i++) {
    for (var j = 0, h = a.height; j < h; j++) {
      assertStructuralEq(a.get(i,j), b.get(i,j));
    }
  }
}

function assertEqWrapArray(a, b) {
  assertEq(a.width, b.width);
  assertEq(a.height, b.height);
  for (var i = 0, w = a.width; i < w; i++) {
    for (var j = 0, h = a.height; j < h; j++) {
      assertStructuralEq(a.get(i,j), b.get(i,j));
    }
  }
}

function assertEqArray(a, b) {
  assertEq(a.length, b.length);
  for (var i = 0, l = a.length; i < l; i++) {
    assertStructuralEq(a[i], b[i]);
  }
}

function assertEqParallelArray(a, b) {
  assertEq(isParArrOrMat(a), true);
  assertEq(isParArrOrMat(b), true);

  var shape = a.shape;
  assertEqArray(shape, b.shape);

  function bump(indices) {
    var d = indices.length - 1;
    while (d >= 0) {
      if (++indices[d] < shape[d])
        break;
      indices[d] = 0;
      d--;
    }
    return d >= 0;
  }

  var iv = shape.map(function () { return 0; });
  do {
    var e1 = a.get.apply(a, iv);
    var e2 = b.get.apply(b, iv);
    assertStructuralEq(e1, e2);
  } while (bump(iv));
}
