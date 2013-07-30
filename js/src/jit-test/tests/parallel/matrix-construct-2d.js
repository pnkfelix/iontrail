load(libdir + "parallelarray-helpers.js");

function copyarray(a) { return function (i, j) { return a[i][j]; }; }

function build2dArray(h, w, f) {
  function buildRow(i) {
    return Array.build(w, function (j) { return f(i,j); });
  }
  return Array.build(h, buildRow);
}

function constructSimple() {
  function kernel(i,j) { return 1 + i*3 + j; }

  var expectedArray = build2dArray(3, 3, kernel);

  // sanity checking build2dArray for simple case
  assertEqArray(expectedArray, [[1, 2, 3],
                                [4, 5, 6],
                                [7, 8, 9]]);

  var mExpect, mActual;

  mExpect = new Matrix([3,3], copyarray(expectedArray));
  mActual = new Matrix([3,3], kernel);
  assertEqMatrix(mActual, mExpect);

  mActual = new Matrix([3,3], ["any"], kernel);
  assertEqMatrix(mActual, mExpect);

  mActual = new Matrix([3], [3, "any"],
    function (i) {
      return new Matrix([3], ["any"], function (j) { return kernel(i,j); });
    });
  assertEqMatrix(mActual, mExpect);
}

function constructAny() {
  function kernel(i,j) { return 1 + i*100 + j; }

  var expectedArray = build2dArray(100, 5, kernel);

  var mExpect, mActual;

  mExpect = new Matrix([100,5], copyarray(expectedArray));
  mActual = new Matrix([100,5], kernel);
  assertEqMatrix(mActual, mExpect);

  mActual = new Matrix([100,5], ["any"], kernel);
  assertEqMatrix(mActual, mExpect);

  mActual = new Matrix([100], [5, "any"],
    function (i) {
      return new Matrix([5], ["any"], function (j) { return kernel(i,j); });
    });
  assertEqMatrix(mActual, mExpect);

  assertParallelModesCommute(function(m) {
    return new Matrix([100,5], kernel, m);
  });

  assertParallelModesCommute(function(m) {
    return new Matrix([100,5], ["any"], kernel, m);
  });

}

function constructNested_DoesNotYetWork() {
  function kernel(i,j) { return 1 + i*100 + j; }

  assertParallelModesCommute(function(m) {
    m.spew = 1;
    function outer(i) {
      // nested constructor call to Matrix is not yet parallelizable.
      return new Matrix([5], ["any"], function (j) { return kernel(i,j); });
    }
    return new Matrix([20], [5, "any"], outer, m);
  });
}

function constructAnyOut() {
  function kernel(i,j) { return 1 + i*100 + j; }

  var expectedArray = build2dArray(100, 5, kernel);

  var mExpect, mActual;

  mExpect = new Matrix([100,5], copyarray(expectedArray));
  mActual = new Matrix([100,5], function (i,j,out) { out.set(kernel(i,j)); });
  assertEqMatrix(mActual, mExpect);

  mActual = new Matrix([100,5], ["any"], function (i,j,out) { out.set(kernel(i,j)); });
  assertEqMatrix(mActual, mExpect);

  mActual = new Matrix([100], [5, "any"],
    function (i, out) { out.gather(function (j) { return kernel(i,j); }); });
  assertEqMatrix(mActual, mExpect);

  mActual = new Matrix([100], [5, "any"],
    function (i, out) { out.gather(function (j, out2) { out2.set(kernel(i,j)); }); });
  assertEqMatrix(mActual, mExpect);


  assertParallelModesCommute(function(m) {
    return new Matrix([100,5], function (i,j,out) { out.set(kernel(i,j)); }, m);
  });

  assertParallelModesCommute(function(m) {
    return new Matrix([100,5],
                      ["any"],
                      function (i,j,out) { out.set(kernel(i,j)); }, m);
  });

  assertParallelModesCommute(function(m) {
    function fill(i,out) {
      out.gather(function (j) { return kernel(i,j); });
    }
    return new Matrix([3], [2,"any"], fill);
  });

  assertParallelModesCommute(function(m) {
    function fill(i,out) {
      out.gather(function (j, out2) { out2.set(kernel(i,j)); });
    }
    return new Matrix([100], [5,"any"], fill);
  });
}

try {
  if (getBuildConfiguration().parallelJS) {
    constructSimple();
    constructAny();
    // constructIsolated_DoesNotYetWork();
    constructAnyOut();
  }
} catch (e) {
  print(e.name);
  print(e.message);
  print(e.stack);
  throw e;
}
