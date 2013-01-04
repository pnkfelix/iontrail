load(libdir + "parallelarray-helpers.js");

function testScatterIdentity() {
  var p = new ParallelArray([1,2,3,4,5]);
  var r = p.scatter([0,1,2,3,4], undefined, undefined, undefined, {mode:"par",merge:"par",noprint:function (x) { print(JSON.stringify(x)); }});
  assertEqParallelArray(p, r);
}

testScatterIdentity();

