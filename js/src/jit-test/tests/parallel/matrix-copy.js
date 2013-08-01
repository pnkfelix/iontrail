// |jit-test| no-ion-limit-script-size
load(libdir + "parallelarray-helpers.js");

function copy1d() {

  var mExpect = new Matrix([100], function (i) { return i+1; });
  var mActual;

  function kernelId(elem, t, source) { return elem; }
  mActual = mExpect.mapPar(kernelId);
  assertEqMatrix(mActual, mExpect);

  function kernelGet(elem, i, source) { return source.get(i); }
  mActual = mExpect.mapPar(kernelGet);
  assertEqMatrix(mActual, mExpect);

  assertParallelModesCommute(function(m) {
    return mExpect.mapPar(kernelId, 1, ["any"], m);
  });


  assertParallelModesCommute(function(m) {
    // m.print = function (x) { print(JSON.stringify(x)); };
    return mExpect.mapPar(kernelGet, 1, ["any"], m); });
}

function copy2d() {

  var mExpect = new Matrix([10, 10], function (i, j) { return i*100+j+1; });
  var mActual;

  // Must update calls that omit depth when default depth beomces 1
  // (instead of .shape.length).

  function kernelId(elem, i, j, source) { return elem; }
  mActual = mExpect.mapPar(kernelId);
  assertEqMatrix(mActual, mExpect);

  function kernelIdOut(elem, i, j, source, out) { out.set(elem); }
  mActual = mExpect.mapPar(kernelIdOut);
  assertEqMatrix(mActual, mExpect);

  function kernelGet(elem, i, j, source) { return source.get(i, j); }
  mActual = mExpect.mapPar(kernelGet);
  assertEqMatrix(mActual, mExpect);

  function kernelGetOut(elem, i, j, source, out) { out.set(source.get(i, j)); }
  mActual = mExpect.mapPar(kernelGetOut);
  assertEqMatrix(mActual, mExpect);

  assertParallelModesCommute(function(m) {
    return mExpect.mapPar(kernelId, 2, ["any"], m);
  });

  assertParallelModesCommute(function(m) {
    return mExpect.mapPar(kernelIdOut, 2, ["any"], m);
  });

  assertParallelModesCommute(function(m) {
    return mExpect.mapPar(kernelGet, 2, ["any"], m);
  });

  assertParallelModesCommute(function(m) {
    return mExpect.mapPar(kernelGetOut, 2, ["any"], m);
  });
}

function copy2dDecoupled() {
  function kernel(i, j) { return i*100+j+1; }
  var mExpect = new Matrix([10, 10], kernel);
  var mFragmented = new Matrix([10],
    function (i) { return new Matrix([10], function (j) { return kernel(i,j); }); });

  var mActual;

  // Must update calls that omit depth when default depth beomces 1
  // (instead of .shape.length).

  function kernelId(elem, i, source) { return elem; }
  mActual = mExpect.mapPar(kernelId, 1, [10, "any"]);
  assertEqMatrix(mActual, mExpect);
  mActual = mExpect.mapPar(kernelId, 1);
  assertEqMatrix(mActual, mFragmented);

  assertParallelModesCommute(function(m) {
    return mExpect.mapPar(kernelId, 2, ["any"], m);
  });


  function kernelIdMap(elem, i, source) {
    return elem.map(function (elem, j, source2) { return elem; });
  }
  mActual = mExpect.mapPar(kernelIdMap, 1, [10, "any"]);
  assertEqMatrix(mActual, mExpect);
  mActual = mExpect.mapPar(kernelIdMap, 1);
  assertEqMatrix(mActual, mFragmented);

  function kernelIdOutLoop(elem, i, source, out) {
    for (var j=0; j < elem.shape[0]; j++) { out.set(j, elem.get(j)); }
  }
  // function kernelIdOut(elem, i, source, out) { out.set(elem); }
  mActual = mExpect.mapPar(kernelIdOutLoop, 1, [10, "any"]);
  assertEqMatrix(mActual, mExpect);

  function kernelIdOut(elem, i, source, out) { out.set(elem); }
  mActual = mExpect.mapPar(kernelIdOut, 1);
  assertEqMatrix(mActual, mFragmented);

  if (false) assertParallelModesCommute(function(m) {
    return mExpect.mapPar(kernelIdOut, 1, ["any"], m);
  });


  function kernelGet1(elem, i, source) { return source.get(i); }
  mActual = mExpect.mapPar(kernelGet1, 1);
  assertEqMatrix(mActual, mFragmented);

  function kernelGet2(elem, i, j, source) { return source.get(i, j); }
  mActual = mExpect.mapPar(kernelGet2, 2);
  assertEqMatrix(mActual, mExpect);

  assertParallelModesCommute(function(m) {
    return mExpect.mapPar(kernelGet2, 2, ["any"], m);
  });

  function kernelGet2Out(elem, i, j, source, out) { out.set(source.get(i, j)); }
  mActual = mExpect.mapPar(kernelGet2Out, 2, ["any"]);
  assertEqMatrix(mActual, mExpect);

  if (false) assertParallelModesCommute(function(m) {
    return mExpect.mapPar(kernelGet2Out, 2, ["any"], m);
  });
}

try {
  if (getBuildConfiguration().parallelJS) {
    copy1d();
    copy2d();
    copy2dDecoupled();
  }
} catch (e) {
  print(e.name);
  print(e.message);
  print(e.stack);
  throw e;
}