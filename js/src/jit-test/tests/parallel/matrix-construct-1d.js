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
  // e.g. AT == Int8Array, ATdesc == "int8"
  function constructTyped(AT, ATdesc) {
    function kernel(i) { return i+200; };
    var m = new Matrix([512], [ATdesc], kernel);
    var a = AT.build(512, function (i) { return i+200; });
    assertEqMatrixTypedArray(m, a, AT);

    assertParallelModesCommute(function(m) {
      return new Matrix([512], [ATdesc], kernel, m);
    });
  }

  constructTyped(Int8Array, "int8");
}
function constructInt8Out() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function constructTypedOut(AT, ATdesc) {
    function kernel(i, out) { out.set(i+200); };
    var m = new Matrix([512], [ATdesc], kernel);
    var a = AT.build(512, function (i) { return i+200; });
    assertEqMatrixTypedArray(m, a, AT);

    assertParallelModesCommute(function(m) {
      return new Matrix([512], [ATdesc], kernel, m);
    });
  }

  constructTypedOut(Int8Array, "int8");
}

function constructUint8() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function constructTyped(AT, ATdesc) {
    function kernel(i) { return i+200; };
    var m = new Matrix([512], [ATdesc], kernel);
    var a = AT.build(512, function (i) { return i+200; });
    assertEqMatrixTypedArray(m, a, AT);
  
    assertParallelModesCommute(function(m) {
      return new Matrix([512], [ATdesc], kernel, m);
    });
  }

  constructTyped(Uint8Array, "uint8");
}
function constructUint8Out() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function constructTypedOut(AT, ATdesc) {
    function kernel(i, out) { out.set(i+200); };
    var m = new Matrix([512], [ATdesc], kernel);
    var a = AT.build(512, function (i) { return i+200; });
    assertEqMatrixTypedArray(m, a, AT);

    assertParallelModesCommute(function(m) {
      return new Matrix([512], [ATdesc], kernel, m);
    });
  }

  constructTypedOut(Uint8Array, "uint8");
}

function constructUint8clamped() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function constructTyped(AT, ATdesc) {
    function kernel(i) { return i+200; };
    var m = new Matrix([512], [ATdesc], kernel);
    var a = AT.build(512, function (i) { return i+200; });
    assertEqMatrixTypedArray(m, a, AT);
  
    assertParallelModesCommute(function(m) {
      return new Matrix([512], [ATdesc], kernel, m);
    });
  }

  constructTyped(Uint8ClampedArray, "uint8clamped");
}
function constructUint8clampedOut() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function constructTypedOut(AT, ATdesc) {
    function kernel(i, out) { out.set(i+200); };
    var m = new Matrix([512], [ATdesc], kernel);
    var a = AT.build(512, function (i) { return i+200; });
    assertEqMatrixTypedArray(m, a, AT);

    assertParallelModesCommute(function(m) {
      return new Matrix([512], [ATdesc], kernel, m);
    });
  }

  constructTypedOut(Uint8ClampedArray, "uint8clamped");
}

function constructInt16() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function constructTyped(AT, ATdesc) {
    function kernel(i) { return i+200; };
    var m = new Matrix([512], [ATdesc], kernel);
    var a = AT.build(512, function (i) { return i+200; });
    assertEqMatrixTypedArray(m, a, AT);
  
    assertParallelModesCommute(function(m) {
      return new Matrix([512], [ATdesc], kernel, m);
    });
  }

  constructTyped(Int16Array, "int16");
}
function constructInt16Out() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function constructTypedOut(AT, ATdesc) {
    function kernel(i, out) { out.set(i+200); };
    var m = new Matrix([512], [ATdesc], kernel);
    var a = AT.build(512, function (i) { return i+200; });
    assertEqMatrixTypedArray(m, a, AT);

    assertParallelModesCommute(function(m) {
      return new Matrix([512], [ATdesc], kernel, m);
    });
  }

  constructTypedOut(Int16Array, "int16");
}

function constructUint16() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function constructTyped(AT, ATdesc) {
    function kernel(i) { return i+200; };
    var m = new Matrix([512], [ATdesc], kernel);
    var a = AT.build(512, function (i) { return i+200; });
    assertEqMatrixTypedArray(m, a, AT);
  
    assertParallelModesCommute(function(m) {
      return new Matrix([512], [ATdesc], kernel, m);
    });
  }

  constructTyped(Uint16Array, "uint16");
}

function constructUint16Out() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function constructTypedOut(AT, ATdesc) {
    function kernel(i, out) { out.set(i+200); };
    var m = new Matrix([512], [ATdesc], kernel);
    var a = AT.build(512, function (i) { return i+200; });
    assertEqMatrixTypedArray(m, a, AT);

    assertParallelModesCommute(function(m) {
      return new Matrix([512], [ATdesc], kernel, m);
    });
  }

  constructTypedOut(Uint16Array, "uint16");
}

function constructInt32() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function constructTyped(AT, ATdesc) {
    function kernel(i) { return i+200; };
    var m = new Matrix([512], [ATdesc], kernel);
    var a = AT.build(512, function (i) { return i+200; });
    assertEqMatrixTypedArray(m, a, AT);
  
    assertParallelModesCommute(function(m) {
      return new Matrix([512], [ATdesc], kernel, m);
    });
  }

  constructTyped(Int32Array, "int32");
}

function constructInt32Out() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function constructTypedOut(AT, ATdesc) {
    function kernel(i, out) { out.set(i+200); };
    var m = new Matrix([512], [ATdesc], kernel);
    var a = AT.build(512, function (i) { return i+200; });
    assertEqMatrixTypedArray(m, a, AT);

    assertParallelModesCommute(function(m) {
      return new Matrix([512], [ATdesc], kernel, m);
    });
  }

  constructTypedOut(Int32Array, "int32");
}

function constructUint32() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function constructTyped(AT, ATdesc) {
    function kernel(i) { return i+200; };
    var m = new Matrix([512], [ATdesc], kernel);
    var a = AT.build(512, function (i) { return i+200; });
    assertEqMatrixTypedArray(m, a, AT);
  
    assertParallelModesCommute(function(m) {
      return new Matrix([512], [ATdesc], kernel, m);
    });
  }

  constructTyped(Uint32Array, "uint32");
}

function constructUint32Out() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function constructTypedOut(AT, ATdesc) {
    function kernel(i, out) { out.set(i+200); };
    var m = new Matrix([512], [ATdesc], kernel);
    var a = AT.build(512, function (i) { return i+200; });
    assertEqMatrixTypedArray(m, a, AT);

    assertParallelModesCommute(function(m) {
      return new Matrix([512], [ATdesc], kernel, m);
    });
  }

  constructTypedOut(Uint32Array, "uint32");
}

function constructFloat32() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function constructTyped(AT, ATdesc) {
    function kernel(i) { return i+200; };
    var m = new Matrix([512], [ATdesc], kernel);
    var a = AT.build(512, function (i) { return i+200; });
    assertEqMatrixTypedArray(m, a, AT);
  
    assertParallelModesCommute(function(m) {
      return new Matrix([512], [ATdesc], kernel, m);
    });
  }

  constructTyped(Float32Array, "float32");
}

function constructFloat32Out() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function constructTypedOut(AT, ATdesc) {
    function kernel(i, out) { out.set(i+200); };
    var m = new Matrix([512], [ATdesc], kernel);
    var a = AT.build(512, function (i) { return i+200; });
    assertEqMatrixTypedArray(m, a, AT);

    assertParallelModesCommute(function(m) {
      return new Matrix([512], [ATdesc], kernel, m);
    });
  }

  constructTypedOut(Float32Array, "float32");
}

function constructFloat64() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function constructTyped(AT, ATdesc) {
    function kernel(i) { return i+200; };
    var m = new Matrix([512], [ATdesc], kernel);
    var a = AT.build(512, function (i) { return i+200; });
    assertEqMatrixTypedArray(m, a, AT);

    assertParallelModesCommute(function(m) {
      return new Matrix([512], [ATdesc], kernel, m);
    });
  }

  constructTyped(Float64Array, "float64");
}

function constructFloat64Out() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function constructTypedOut(AT, ATdesc) {
    function kernel(i, out) { out.set(i+200); };
    var m = new Matrix([512], [ATdesc], kernel);
    var a = AT.build(512, function (i) { return i+200; });
    assertEqMatrixTypedArray(m, a, AT);

    assertParallelModesCommute(function(m) {
      return new Matrix([512], [ATdesc], kernel, m);
    });
  }

  constructTypedOut(Float64Array, "float64");
}

try {
  if (getBuildConfiguration().parallelJS) {
    constructAny();
    constructInt8();
    constructUint8();
    constructUint8clamped();
    constructInt16();
    constructUint16();
    constructInt32();
    constructUint32();
    constructFloat32();
    constructFloat64();

    constructAnyOut();
    constructInt8Out();
    constructUint8Out();
    constructUint8clampedOut();
    constructInt16Out();
    constructUint16Out();
    constructInt32Out();
    constructUint32Out();
    constructFloat32Out();
    constructFloat64Out();
  }
} catch (e) {
  print(e.name);
  print(e.message);
  print(e.stack);
  throw e;
}
