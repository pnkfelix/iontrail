// |reftest| skip-if(!this.hasOwnProperty("TypedObject"))

var BUGNUMBER = 578700;
var summary = 'Binary Data class diagram';

function assertNotEq(a, b) {
    var ok = false;
    try {
        assertEq(a, b);
    } catch(exc) {
        ok = true;
    }

    if (!ok)
        throw new TypeError("Assertion failed: assertNotEq(" + a + " " + b + ")");
}

function assertThrows(f) {
    var ok = false;
    try {
        f();
    } catch (exc) {
        ok = true;
    }
    if (!ok)
        throw new TypeError("Assertion failed: " + f + " did not throw as expected");
}

function runTests() {
  print(BUGNUMBER + ": " + summary);

  var ArrayType = TypedObject.ArrayType;
  var StructType = TypedObject.StructType;

  assertEq(true, ArrayType instanceof Function);
  assertEq(true, ArrayType.prototype instanceof Function);

  assertEq(ArrayType.__proto__, Function.__proto__);
  assertEq(ArrayType.prototype.__proto__, Function.__proto__);

  assertEq(true, StructType instanceof Function);
  assertEq(true, StructType.prototype instanceof Function);

  assertEq(StructType.__proto__, Function.__proto__);
  assertEq(StructType.prototype.__proto__, Function.__proto__);

  if (typeof reportCompare === "function")
    reportCompare(true, true);

  print("Tests complete");
}

runTests();
