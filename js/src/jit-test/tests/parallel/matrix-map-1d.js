load(libdir + "parallelarray-helpers.js");

function Array_get (i) { return this[i]; };
Array.prototype.get = Array_get;
Int8Array.prototype.get = Array_get;
Int16Array.prototype.get = Array_get;
Int32Array.prototype.get = Array_get;
Uint8Array.prototype.get = Array_get;
Uint16Array.prototype.get = Array_get;
Uint32Array.prototype.get = Array_get;
Uint8ClampedArray.prototype.get = Array_get;
Float32Array.prototype.get = Array_get;
Float64Array.prototype.get = Array_get;

function make_map (AT) {
  function MyArray_map (f) {
    var self = this;
    function fill(idx) { return f(self[idx], idx, self); };
    return AT.build(self.length, fill);
  };
  return MyArray_map;
}

Int8Array.prototype.map = make_map(Int8Array);
Int16Array.prototype.map = make_map(Int16Array);
Int32Array.prototype.map = make_map(Int32Array);
Uint8Array.prototype.map = make_map(Uint8Array);
Uint16Array.prototype.map = make_map(Uint16Array);
Uint32Array.prototype.map = make_map(Uint32Array);
Uint8ClampedArray.prototype.map = make_map(Uint8ClampedArray);
Float32Array.prototype.map = make_map(Float32Array);
Float64Array.prototype.map = make_map(Float64Array);

function mapAny() {
  function kernel(elem, idx, source) {
    return elem+idx+source.get(0);
  }
  function cxnKernel(idx) { return idx + 1; }
  var m1 = new Matrix([256], cxnKernel);
  var a1 = Array.build(256, cxnKernel);

  var a2 = a1.map(kernel);

  var m2implicit = m1.mapPar(kernel);
  assertEqMatrixArray(m2implicit, a2);

  var m2explicit = m1.mapPar(kernel, 1, ["any"]);
  assertEqMatrixArray(m2explicit, a2);

  assertParallelModesCommute(function(m) {
    return m1.mapPar(kernel);
  });

  assertParallelModesCommute(function(m) {
    return m1.mapPar(kernel, 1, ["any"]);
  });
}

function mapAnyOut() {
  function kernel(elem, idx, source) {
    return elem+idx+source.get(0);
  }
  function mtxKernel(elem, idx, source, outptr) {
    outptr.set(kernel(elem, idx, source));
  }
  function cxnKernel(idx) { return idx + 1; }
  var m1 = new Matrix([256], cxnKernel);
  var a1 = Array.build(256, cxnKernel);

  var a2 = a1.map(kernel);

  var m2implicit = m1.mapPar(mtxKernel);
  assertEqMatrixArray(m2implicit, a2);

  var m2explicit = m1.mapPar(mtxKernel, 1, ["any"]);
  assertEqMatrixArray(m2explicit, a2);

  assertParallelModesCommute(function(m) {
    return m1.mapPar(kernel);
  });

  assertParallelModesCommute(function(m) {
    return m1.mapPar(kernel, 1, ["any"]);
  });
}

// TODO: Currently all tests are [T] -> [T] maps rather than [X] ->
// [T] maps; add variants that map from a different source array
// type to the targetted destination type.

function mapInt8() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function mapTyped(AT, ATdesc) {
    function kernel(elem, idx, source) {
      return elem + idx + source.get(0);
    }
    function cxnKernel(i) { return i+1; };
    var m1 = new Matrix([64], [ATdesc], cxnKernel);
    var a1 = AT.build(64, cxnKernel);

    var a2 = a1.map(kernel);

    var m2 = m1.mapPar(kernel, 1, [ATdesc]);

    // print(JSON.stringify(a2));
    // print(m2);
    assertEqMatrixTypedArray(m2, a2, AT);

    assertParallelModesCommute(function(m) {
      return m1.mapPar(kernel, 1, [ATdesc]);
    });
  }

  mapTyped(Int8Array, "int8");
}

function mapInt8Out() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function mapTyped(AT, ATdesc) {
    function kernel(elem, idx, source) {
      return elem + idx + source.get(0);
    }
    function mtxKernel(elem, idx, source, outptr) {
      outptr.set(kernel(elem, idx, source));
    }
    function cxnKernel(i) { return i+1; };
    var m1 = new Matrix([512], [ATdesc], cxnKernel);
    var a1 = AT.build(512, cxnKernel);

    var a2 = a1.map(kernel);

    var m2 = m1.mapPar(mtxKernel, 1, [ATdesc]);
    assertEqMatrixTypedArray(m2, a2, AT);

    assertParallelModesCommute(function(m) {
      return m1.mapPar(mtxKernel, 1, [ATdesc]);
    });
  }

  mapTyped(Int8Array, "int8");
}

function mapInt16() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function mapTyped(AT, ATdesc) {
    function kernel(elem, idx, source) {
      return elem + idx + source.get(0);
    }
    function cxnKernel(i) { return i+1; };
    var m1 = new Matrix([64], [ATdesc], cxnKernel);
    var a1 = AT.build(64, cxnKernel);

    var a2 = a1.map(kernel);

    var m2 = m1.mapPar(kernel, 1, [ATdesc]);

    // print(JSON.stringify(a2));
    // print(m2);
    assertEqMatrixTypedArray(m2, a2, AT);

    assertParallelModesCommute(function(m) {
      return m1.mapPar(kernel, 1, [ATdesc]);
    });
  }

  mapTyped(Int16Array, "int16");
}

function mapInt16Out() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function mapTyped(AT, ATdesc) {
    function kernel(elem, idx, source) {
      return elem + idx + source.get(0);
    }
    function mtxKernel(elem, idx, source, outptr) {
      outptr.set(kernel(elem, idx, source));
    }
    function cxnKernel(i) { return i+1; };
    var m1 = new Matrix([512], [ATdesc], cxnKernel);
    var a1 = AT.build(512, cxnKernel);

    var a2 = a1.map(kernel);

    var m2 = m1.mapPar(mtxKernel, 1, [ATdesc]);
    assertEqMatrixTypedArray(m2, a2, AT);

    assertParallelModesCommute(function(m) {
      return m1.mapPar(mtxKernel, 1, [ATdesc]);
    });
  }

  mapTyped(Int16Array, "int16");
}

function mapInt32() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function mapTyped(AT, ATdesc) {
    function kernel(elem, idx, source) {
      return elem + idx + source.get(0);
    }
    function cxnKernel(i) { return i+1; };
    var m1 = new Matrix([64], [ATdesc], cxnKernel);
    var a1 = AT.build(64, cxnKernel);

    var a2 = a1.map(kernel);

    var m2 = m1.mapPar(kernel, 1, [ATdesc]);

    // print(JSON.stringify(a2));
    // print(m2);
    assertEqMatrixTypedArray(m2, a2, AT);

    assertParallelModesCommute(function(m) {
      return m1.mapPar(kernel, 1, [ATdesc]);
    });
  }

  mapTyped(Int32Array, "int32");
}

function mapInt32Out() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function mapTyped(AT, ATdesc) {
    function kernel(elem, idx, source) {
      return elem + idx + source.get(0);
    }
    function mtxKernel(elem, idx, source, outptr) {
      outptr.set(kernel(elem, idx, source));
    }
    function cxnKernel(i) { return i+1; };
    var m1 = new Matrix([512], [ATdesc], cxnKernel);
    var a1 = AT.build(512, cxnKernel);

    var a2 = a1.map(kernel);

    var m2 = m1.mapPar(mtxKernel, 1, [ATdesc]);
    assertEqMatrixTypedArray(m2, a2, AT);

    assertParallelModesCommute(function(m) {
      return m1.mapPar(mtxKernel, 1, [ATdesc]);
    });
  }

  mapTyped(Int32Array, "int32");
}

function mapUint8() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function mapTyped(AT, ATdesc) {
    function kernel(elem, idx, source) {
      return elem + idx + source.get(0);
    }
    function cxnKernel(i) { return i+1; };
    var m1 = new Matrix([64], [ATdesc], cxnKernel);
    var a1 = AT.build(64, cxnKernel);

    var a2 = a1.map(kernel);

    var m2 = m1.mapPar(kernel, 1, [ATdesc]);

    // print(JSON.stringify(a2));
    // print(m2);
    assertEqMatrixTypedArray(m2, a2, AT);

    assertParallelModesCommute(function(m) {
      return m1.mapPar(kernel, 1, [ATdesc]);
    });
  }

  mapTyped(Uint8Array, "uint8");
}

function mapUint8Out() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function mapTyped(AT, ATdesc) {
    function kernel(elem, idx, source) {
      return elem + idx + source.get(0);
    }
    function mtxKernel(elem, idx, source, outptr) {
      outptr.set(kernel(elem, idx, source));
    }
    function cxnKernel(i) { return i+1; };
    var m1 = new Matrix([512], [ATdesc], cxnKernel);
    var a1 = AT.build(512, cxnKernel);

    var a2 = a1.map(kernel);

    var m2 = m1.mapPar(mtxKernel, 1, [ATdesc]);
    assertEqMatrixTypedArray(m2, a2, AT);

    assertParallelModesCommute(function(m) {
      return m1.mapPar(mtxKernel, 1, [ATdesc]);
    });
  }

  mapTyped(Uint8Array, "uint8");
}

function mapUint8clamped() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function mapTyped(AT, ATdesc) {
    function kernel(elem, idx, source) {
      return elem + idx + source.get(0);
    }
    function cxnKernel(i) { return i+1; };
    var m1 = new Matrix([64], [ATdesc], cxnKernel);
    var a1 = AT.build(64, cxnKernel);

    var a2 = a1.map(kernel);

    var m2 = m1.mapPar(kernel, 1, [ATdesc]);

    // print(JSON.stringify(a2));
    // print(m2);
    assertEqMatrixTypedArray(m2, a2, AT);

    assertParallelModesCommute(function(m) {
      return m1.mapPar(kernel, 1, [ATdesc]);
    });
  }

  mapTyped(Uint8ClampedArray, "uint8clamped");
}

function mapUint8clampedOut() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function mapTyped(AT, ATdesc) {
    function kernel(elem, idx, source) {
      return elem + idx + source.get(0);
    }
    function mtxKernel(elem, idx, source, outptr) {
      outptr.set(kernel(elem, idx, source));
    }
    function cxnKernel(i) { return i+1; };
    var m1 = new Matrix([512], [ATdesc], cxnKernel);
    var a1 = AT.build(512, cxnKernel);

    var a2 = a1.map(kernel);

    var m2 = m1.mapPar(mtxKernel, 1, [ATdesc]);
    assertEqMatrixTypedArray(m2, a2, AT);

    assertParallelModesCommute(function(m) {
      return m1.mapPar(mtxKernel, 1, [ATdesc]);
    });
  }

  mapTyped(Uint8ClampedArray, "uint8clamped");
}

function mapUint16() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function mapTyped(AT, ATdesc) {
    function kernel(elem, idx, source) {
      return elem + idx + source.get(0);
    }
    function cxnKernel(i) { return i+1; };
    var m1 = new Matrix([64], [ATdesc], cxnKernel);
    var a1 = AT.build(64, cxnKernel);

    var a2 = a1.map(kernel);

    var m2 = m1.mapPar(kernel, 1, [ATdesc]);

    // print(JSON.stringify(a2));
    // print(m2);
    assertEqMatrixTypedArray(m2, a2, AT);

    assertParallelModesCommute(function(m) {
      return m1.mapPar(kernel, 1, [ATdesc]);
    });
  }

  mapTyped(Uint16Array, "uint16");
}

function mapUint16Out() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function mapTyped(AT, ATdesc) {
    function kernel(elem, idx, source) {
      return elem + idx + source.get(0);
    }
    function mtxKernel(elem, idx, source, outptr) {
      outptr.set(kernel(elem, idx, source));
    }
    function cxnKernel(i) { return i+1; };
    var m1 = new Matrix([512], [ATdesc], cxnKernel);
    var a1 = AT.build(512, cxnKernel);

    var a2 = a1.map(kernel);

    var m2 = m1.mapPar(mtxKernel, 1, [ATdesc]);
    assertEqMatrixTypedArray(m2, a2, AT);

    assertParallelModesCommute(function(m) {
      return m1.mapPar(mtxKernel, 1, [ATdesc]);
    });
  }

  mapTyped(Uint16Array, "uint16");
}

function mapUint32() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function mapTyped(AT, ATdesc) {
    function kernel(elem, idx, source) {
      return elem + idx + source.get(0);
    }
    function cxnKernel(i) { return i+1; };
    var m1 = new Matrix([64], [ATdesc], cxnKernel);
    var a1 = AT.build(64, cxnKernel);

    var a2 = a1.map(kernel);

    var m2 = m1.mapPar(kernel, 1, [ATdesc]);

    // print(JSON.stringify(a2));
    // print(m2);
    assertEqMatrixTypedArray(m2, a2, AT);

    assertParallelModesCommute(function(m) {
      return m1.mapPar(kernel, 1, [ATdesc]);
    });
  }

  mapTyped(Uint32Array, "uint32");
}

function mapUint32Out() {
  // e.g. AT == Int8Array, ATdesc == "int8"
  function mapTyped(AT, ATdesc) {
    function kernel(elem, idx, source) {
      return elem + idx + source.get(0);
    }
    function mtxKernel(elem, idx, source, outptr) {
      outptr.set(kernel(elem, idx, source));
    }
    function cxnKernel(i) { return i+1; };
    var m1 = new Matrix([512], [ATdesc], cxnKernel);
    var a1 = AT.build(512, cxnKernel);

    var a2 = a1.map(kernel);

    var m2 = m1.mapPar(mtxKernel, 1, [ATdesc]);
    assertEqMatrixTypedArray(m2, a2, AT);

    assertParallelModesCommute(function(m) {
      return m1.mapPar(mtxKernel, 1, [ATdesc]);
    });
  }

  mapTyped(Uint32Array, "uint32");
}


try {
  if (getBuildConfiguration().parallelJS) {
    mapAny();
    mapInt8();
    mapInt16();
    mapInt32();
    mapUint8();
    mapUint8clamped();
    mapUint16();
    mapUint32();

    mapAnyOut();
    mapInt8Out();
    mapInt16Out();
    mapInt32Out();
    mapUint8Out();
    mapUint8clampedOut();
    mapUint16Out();
    mapUint32Out();

  }
} catch (e) {
  print(e.name);
  print(e.message);
  print(e.stack);
  throw e;
}
