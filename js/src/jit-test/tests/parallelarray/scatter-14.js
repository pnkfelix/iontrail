load(libdir + "parallelarray-helpers.js");

function testScatterIdentity() {
  var p = new ParallelArray([1,2,3,4,5,6,7,8,9,10]);
  var m = {mode:"par", strategy: "divide-scatter-vector", expect: "any"};
  var r = p.scatter([0,1,2,3,4,5,6,7,8,9], 0, undefined, 10, m);
  assertEqParallelArray(p, r);
}

testScatterIdentity();
