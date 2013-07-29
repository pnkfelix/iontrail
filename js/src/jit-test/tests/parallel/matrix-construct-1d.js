load(libdir + "parallelarray-helpers.js");

function constructAny() {
  function kernel(i) { return i+1; };
  var m1 = new Matrix([256], kernel);
  var a = Array.build(256, kernel);
  assertEqMatrixArray(m1, a);

  var m2 = new Matrix([256], ["any"], kernel);
  assertEqMatrixArray(m2, a);

  assertParallelModesCommute(function(m) {
    return new Matrix([256], kernel, m);
  });

  assertParallelModesCommute(function(m) {
    return new Matrix([256], ["any"], kernel, m);
  });
}

function constructAnyOut() {
  function kernel(i, out) { out.set(i+1); };
  var m1 = new Matrix([256], kernel);
  var a = Array.build(256, function (i) { return i+1; });
  assertEqMatrixArray(m1, a);

  var m2 = new Matrix([256], ["any"], kernel);
  assertEqMatrixArray(m2, a);

  assertParallelModesCommute(function(m) {
    return new Matrix([256], kernel, m);
  });

  assertParallelModesCommute(function(m) {
    return new Matrix([256], ["any"], kernel, m);
  });
}

function buildArray(len, f) {
  var AT = this;
  var a = new AT(len);
  for (var i=0; i < len; i++) { a[i] = f(i); }
  return a;
}

Int8Array.build = buildArray;
Int16Array.build = buildArray;
Int32Array.build = buildArray;
Uint8Array.build = buildArray;
Uint8ClampedArray.build = buildArray;
Uint16Array.build = buildArray;
Uint32Array.build = buildArray;
Float32Array.build = buildArray;
Float64Array.build = buildArray;

function constructInt8() {
  function kernel(i) { return i+200; };
  var m = new Matrix([512], ["int8"], kernel);
  var a = Int8Array.build(512, function (i) { return i+200; });
  assertEqMatrixTypedArray(m, a, Int8Array);

  assertParallelModesCommute(function(m) {
    return new Matrix([512], ["int8"], kernel, m);
  });
}

function constructInt8Out() {
  function kernel(i, out) { out.set(i+200); };
  var m = new Matrix([512], ["int8"], kernel);
  var a = Int8Array.build(512, function (i) { return i+200; });
  assertEqMatrixTypedArray(m, a, Int8Array);

  assertParallelModesCommute(function(m) {
    return new Matrix([512], ["int8"], kernel, m);
  });
}


try {
  if (getBuildConfiguration().parallelJS) {
    constructAny();
    constructInt8();
/*
    constructUint8();
    constructUint8clamped();
    constructInt16();
    constructUint16();
    constructInt32();
    constructUint32();
    constructFloat32();
    constructFloat64();
*/

    constructAnyOut();
    constructInt8Out();
/*
    constructUint8Out();
    constructUint8clampedOut();
    constructInt16Out();
    constructUint16Out();
    constructInt32Out();
    constructUint32Out();
    constructFloat32Out();
    constructFloat64Out();
*/
  }
} catch (e) {
  print(e.name);
  print(e.message);
  print(e.stack);
  throw e;
}
