// This file expects the lib/parallelarray-helpers.js file
// to already be loaded when you run it.
// I.e. when using js shell, run in this fashion:
//
// js -f path/to/parallelarray-helpers.js -f path/to/this-file.js

test();

function test()
{
  function id(x) { return x; }

  for (var k = 100; k > 0; k--)
  {
    var len = k;
    var p = new ParallelArray(len, id);
    var idx = build(len, id);

    print("k:"+k);

    var seqval = p.scatter(idx, 0, undefined, len, {mode:"seq"});
    var parval = p.scatter(idx, 0, undefined, len,
                           {mode:"par", strategy:"divide-scatter-vector", expect:"any", print:print});

      // assertEqParallelArray(parval, parval);
      assertEqParallelArray(seqval, parval);
  }
}
