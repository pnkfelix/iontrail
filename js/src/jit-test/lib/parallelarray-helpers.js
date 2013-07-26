/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Explanation of minItemsTestingThreshold:
//
// If the volume of input items in a test is small, then all of them
// may be processed during warmup alone, and the parallel-invocation
// will trivially succeed even if we are intentionally trying to
// detect a failure.
//
// The maximum number of items processed by sequential warmups for
// ArrayBuildPar is:
//      maxSeqItems = maxBailouts * numSlices * CHUNK_SIZE
//
// For maxBailouts = 3, maxSeqItems == 3 * 8 * 32 == 768
// For maxBailouts = 5, maxSeqItems == 5 * 8 * 32 == 1280
//
// Our test code does not have access to the values of these constants
// (maxBailouts, numSlices, CHUNK_SIZE).  Therefore, the value of
// minItemsTestingThreshold should be kept in sync with some value
// greater than maxSeqItems as calculated above.
//
// This is still imperfect since it assumes numSlices <= 8, but
// numSlices is machine-dependent.
// (TODO: consider exposing numSlices via builtin/TestingFunctions.cpp)

var minItemsTestingThreshold = 1024;

// The standard sequence of modes to test.
// First mode compiles for parallel exec.
// Second mode checks that parallel exec does not bail.
// Final mode tests the sequential fallback path.
var MODE_STRINGS = ["compile", "par", "seq"];
var MODES = MODE_STRINGS.map(s => ({mode: s}));

var INVALIDATE_MODE_STRINGS = ["seq", "compile", "par", "seq"];
var INVALIDATE_MODES = INVALIDATE_MODE_STRINGS.map(s => ({mode: s}));

function build(n, f) {
  var result = [];
  for (var i = 0; i < n; i++)
    result.push(f(i));
  return result;
}

function range(n, m) {
  // Returns an array with [n..m] (include on n, exclusive on m)

  var result = [];
  for (var i = n; i < m; i++)
    result.push(i);
  return result;
}

function seq_scan(array, f) {
  // Simple sequential version of scan() that operates over an array

  var result = [];
  result[0] = array[0];
  for (var i = 1; i < array.length; i++) {
    result[i] = f(result[i-1], array[i]);
  }
  return result;
}

function assertAlmostEq(v1, v2) {
  if (v1 === v2)
    return true;
  // + and other fp ops can vary somewhat when run in parallel!
  assertEq(typeof v1, "number");
  assertEq(typeof v2, "number");
  var diff = Math.abs(v1 - v2);
  var percent = diff / v1 * 100.0;
  print("v1 = " + v1);
  print("v2 = " + v2);
  print("% diff = " + percent);
  assertEq(percent < 1e-10, true); // off by an less than 1e-10%...good enough.
}

function assertStructuralEq(e1, e2) {
    if (e1 instanceof Matrix && e2 instanceof Matrix) {
      assertEqMatrix(e1, e2);
    } else if (e1 instanceof ParallelArray && e2 instanceof ParallelArray) {
      assertEqParallelArray(e1, e2);
    } else if (e1 instanceof Array && e2 instanceof ParallelArray) {
      assertEqParallelArrayArray(e2, e1);
    } else if (e1 instanceof ParallelArray && e2 instanceof Array) {
      assertEqParallelArrayArray(e1, e2);
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

function assertEqParallelArrayArray(a, b) {
  assertEq(a.shape.length, 1);
  assertEq(a.length, b.length);
  for (var i = 0, l = a.length; i < l; i++) {
    try {
      assertStructuralEq(a.get(i), b[i]);
    } catch (e) {
      print("...in index ", i, " of ", l);
      throw e;
    }
  }
}

function assertEqMatrixArraylike(a, b) {
  assertEq(a.shape.length, 1);
  assertEq(a.shape[0], b.length);
  for (var i = 0, l = a.shape[0]; i < l; i++) {
    try {
      assertStructuralEq(a.get(i), b[i]);
    } catch (e) {
      print("...in index ", i, " of ", l);
      throw e;
    }
  }
}

function assertEqArray(a, b) {
    assertEq(a.length, b.length);
    for (var i = 0, l = a.length; i < l; i++) {
      try {
        assertStructuralEq(a[i], b[i]);
      } catch (e) {
        print("...in index ", i, " of ", l);
        throw e;
      }
    }
}

function assertEqParallelGettableStructure(a, b) {
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
    try {
      var e1 = a.get.apply(a, iv);
      var e2 = b.get.apply(b, iv);
      assertStructuralEq(e1, e2);
    } catch (e) {
      print("...in indices ", iv, " of ", shape);
      throw e;
    }
  } while (bump(iv));
}

function assertEqMatrix(a, b) {
  assertEq(a instanceof Matrix, true);
  assertEq(b instanceof Matrix, true);

  assertEqParallelGettableStructure(a, b);
}

function assertEqMatrixArray(a, b) {
  assertEq(a instanceof Matrix, true);
  assertEq(b instanceof Array, true);

  assertEqMatrixArraylike(a, b);
}

function assertEqMatrixTypedArray(a, b, WhichTypedArray) {
  assertEq(a instanceof Matrix, true);
  assertEq(b instanceof WhichTypedArray, true);

  assertEqMatrixArraylike(a, b);
}

function assertEqParallelArray(a, b) {
  assertEq(a instanceof ParallelArray, true);
  assertEq(b instanceof ParallelArray, true);

  assertEqParallelGettableStructure(a, b);
}

// Checks that whenever we execute this in parallel mode,
// it bails out. `opFunction` should be a closure that takes a
// mode parameter and performs some parallel array operation.
// This closure will be invoked repeatedly.
//
// Here is an example of the expected usage:
//
//    assertParallelExecWillBail(function(m) {
//        new ParallelArray(..., m)
//    });
//
// where the `new ParallelArray(...)` is a stand-in
// for some parallel array operation.
function assertParallelExecWillBail(opFunction) {
  opFunction({mode:"compile"}); // get the script compiled
  opFunction({mode:"bailout"}); // check that it bails when executed
}

// Checks that when we execute this in parallel mode,
// some bailouts will occur but we will recover and
// return to parallel execution mode. `opFunction` is a closure
// that expects a mode, just as in `assertParallelExecWillBail`.
function assertParallelExecWillRecover(opFunction) {
  opFunction({mode:"compile"}); // get the script compiled
  opFunction({mode:"recover"}); // check that it bails when executed
}

// Checks that we will (eventually) be able to compile and exection
// `opFunction` in parallel mode. Invokes `cmpFunction` with the
// result.  For some tests, it takes many compile rounds to reach a TI
// fixed point. So this function will repeatedly attempt to invoke
// `opFunction` with `compile` and then `par` mode until getting a
// successful `par` run.  After enough tries, of course, we give up
// and declare a test failure.
function assertParallelExecSucceeds(opFunction, cmpFunction) {
  var failures = 0;
  while (true) {
    print("Attempting compile #", failures);
    var result = opFunction({mode:"compile"});
    cmpFunction(result);

    try {
      print("Attempting parallel run #", failures);
      var result = opFunction({mode:"par"});
      cmpFunction(result);
      break;
    } catch (e) {
      failures++;
      if (failures > 5) {
        throw e; // doesn't seem to be reaching a fixed point!
      } else {
        print(e);
      }
    }
  }

  print("Attempting sequential run");
  var result = opFunction({mode:"seq"});
  cmpFunction(result);
}

// Compares an Array constructed in parallel against one constructed
// sequentially. `func` should be the closure to provide as argument. For
// example:
//
//    assertArraySeqParResultsEq([1, 2, 3], "map", i => i + 1)
//
// would check that `[1, 2, 3].map(i => i+1)` and `[1, 2, 3].mapPar(i => i+1)`
// yield the same result.
//
// Based on `assertParallelExecSucceeds`
function assertArraySeqParResultsEq(arr, op, func, cmpFunc) {
  if (!cmpFunc)
    cmpFunc = assertStructuralEq;
  var expected = arr[op].apply(arr, [func]);
  assertParallelExecSucceeds(
    function (m) { return arr[op + "Par"].apply(arr, [func, m]); },
    function (r) { cmpFunc(expected, r); });
}

// Compares a ParallelArray function against its equivalent on the
// `Array` prototype. `func` should be the closure to provide as
// argument. For example:
//
//    compareAgainstArray([1, 2, 3], "map", i => i + 1)
//
// would check that `[1, 2, 3].map(i => i+1)` and `new
// ParallelArray([1, 2, 3]).map(i => i+1)` yield the same result.
//
// Based on `assertParallelExecSucceeds`
function compareAgainstArray(jsarray, opname, func, cmpFunction) {
  if (!cmpFunction)
    cmpFunction = assertStructuralEq;
  var expected = jsarray[opname].apply(jsarray, [func]);
  var parray = new ParallelArray(jsarray);
  assertParallelExecSucceeds(
    function(m) {
      return parray[opname].apply(parray, [func, m]);
    },
    function(r) {
      cmpFunction(expected, r);
    });
}

// Similar to `compareAgainstArray`, but for the `scan` method which
// does not appear on array.
function testArrayScanPar(jsarray, func, cmpFunction) {
  if (!cmpFunction)
    cmpFunction = assertStructuralEq;
  var expected = seq_scan(jsarray, func);

  // Unfortunately, it sometimes happens that running 'par' twice in a
  // row causes bailouts and other unfortunate things!

  assertParallelExecSucceeds(
    function(m) {
      print(m.mode + " " + m.expect);
      var p = jsarray.scanPar(func, m);
      return p;
    },
    function(r) {
      cmpFunction(expected, r);
    });
}

// Similar to `compareAgainstArray`, but for the `scatter` method.
// In this case, because scatter is so complex, we do not attempt
// to compute the expected result and instead simply invoke
// `cmpFunction(r)` with the result `r` of the scatter operation.
function testScatter(opFunction, cmpFunction) {
  var strategies = ["divide-scatter-version", "divide-output-range"];
  for (var i in strategies) {
    assertParallelExecSucceeds(
      function(m) {
        var m1 = {mode: m.mode,
                  strategy: strategies[i]};
        print(JSON.stringify(m1));
        return opFunction(m1);
      },
      cmpFunction);
  }
}

// Checks that `opFunction`, when run with each of the modes
// in `modes`, returns the same value each time.
function assertParallelModesCommute(opFunction) {
  var expected = opFunction({mode:"seq"});
  assertParallelExecSucceeds(
    opFunction,
    function(r) {
      assertStructuralEq(expected, r);
    });
}

function viewToSource2d(view, width, height, payload) {
  var i=0;
  var ret = "[";
  var matrixNeedsNewline = false;
  for (var row=0; row < height; row++) {
    if (matrixNeedsNewline)
      ret += ",\n ";
    ret += "[";
    var rowNeedsComma = false;
    for (var x=0; x < width; x++) {
      if (rowNeedsComma)
        ret += ", ";
      if (payload == 1) {
        var val = view(i);
        if (val !== undefined)
          ret += val;
        i++;
      } else {
        var entryNeedsComma = false;
        ret += "(";
        for (var k=0; k < payload; k++) {
          // Might be inefficient (does JavaScript have
          // StringBuffers?, or use them internally, like Tamarin?)
          if (entryNeedsComma)
            ret += ", ";
          var val = view(i);
          if (val !== undefined)
            ret += val;
          entryNeedsComma = true;
          i++;
        }
        ret += ")";
      }
      rowNeedsComma = true;
    }
    ret += "]";
    matrixNeedsNewline = true;
  }
  ret += "]";
  return ret;
}

function dbprint(x) {
  // print(x);
}

Matrix.prototype.toSource =
  function toSource() {
    var self = this;
    var slen = self.shape.length;
    if (slen == 1) {
      return "[" + this.buffer.join(",") + "]";
    } else {
      var w = self.shape[0];
      var h = self.shape[1];
      var p = 1;
      for (var i = 2; i < slen; i++) {
        p *= self.shape[i];
      }
      return viewToSource2d(function (j) { dbprint("view("+j+")"); return self.buffer[self.offset+j];}, w, h, p );
    }
  };
