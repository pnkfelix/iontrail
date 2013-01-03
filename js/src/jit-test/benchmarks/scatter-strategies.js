// This file expects the lib/parallelarray-helpers.js file
// to already be loaded when you run it.
// I.e. when using js shell, run in this fashion:
//
// js -f path/to/parallelarray-helpers.js -f path/to/this-file.js

test();

function fillstr(n, l, s, r) {
  s = ""+s;
  while (s.length < n) {
    s = (l + s + r);
  }
  return s;
}

function test()
{
  var start = 0;
  var stop  = 0;
  var modes = [{mode:"seq", strategy:""},
               {mode:"par", strategy:"scatter-vector"},
               {mode:"par", strategy:"output-range"}];

  function id(x) { return x; }
  function plus(x,y) { return x + y; }

  // [A, B, C, ...] -> [A, B, C, ...]
  function makeIdentityIdx(sourceLen, outputLen) {
    return build(sourceLen, id);
  }

  // [A, B, C, ..., A', B', C', ..., A'', B'', C'', ...]
  //   -> [A+A'+A''+..., B+B'+B''+..., C+C'+C''+..., ...]
  function makeRotateIdx(sourceLen, outputLen) {
    return build(sourceLen, function(x) { return x % outputLen; });
  }

  // [A, B, C, ..., A', B', C' ..., A'', B'', C'', ...]
  //   -> [A+B+C+..., A'+B'+C'+..., A''+B''+C''+..., ...]
  function makeReduceIdx(sourceLen, outputLen) {
    var factor = (sourceLen / outputLen) | 0;
    return build(sourceLen, function(x) { return (x / factor) | 0});
  }

  var lil_iters = 5;
  var mid_iters = 20;
  var big_iters = 100;
  var inputs =
    [
     {iters:lil_iters, source:"1e4",               index:makeIdentityIdx},
     {iters:lil_iters, source:"100", output:"20",  index:makeRotateIdx},

     {iters:mid_iters, source:"1e5",               index:makeIdentityIdx},
     {iters:mid_iters, source:"2e5",               index:makeIdentityIdx},
     {iters:mid_iters, source:"4e5",               index:makeIdentityIdx},
     {iters:mid_iters, source:"8e5",               index:makeIdentityIdx},

     {iters:lil_iters, source:"1e6",               index:makeIdentityIdx},
     {iters:lil_iters, source:"2e6",               index:makeIdentityIdx},
     {iters:lil_iters, source:"3e6",               index:makeIdentityIdx},
     {iters:lil_iters, source:"4e6",               index:makeIdentityIdx},
     {iters:lil_iters, source:"8e6",               index:makeIdentityIdx},
     {iters:lil_iters, source:"1e7",               index:makeIdentityIdx},
     {iters:lil_iters, source:"2e7",               index:makeIdentityIdx},

     {iters:lil_iters, source:"2e7", output:"2e7", index:makeRotateIdx},
     {iters:lil_iters, source:"2e7", output:"1e7", index:makeRotateIdx},
     {iters:lil_iters, source:"2e7", output:"9e6", index:makeRotateIdx},
     {iters:lil_iters, source:"2e7", output:"8e6", index:makeRotateIdx},
     {iters:lil_iters, source:"2e7", output:"7e6", index:makeRotateIdx},
     {iters:lil_iters, source:"2e7", output:"6e6", index:makeRotateIdx},
     {iters:lil_iters, source:"2e7", output:"4e6", index:makeRotateIdx},
     {iters:lil_iters, source:"2e7", output:"2e6", index:makeRotateIdx},
     {iters:lil_iters, source:"2e7", output:"1e6", index:makeRotateIdx},
     {iters:lil_iters, source:"2e7", output:"8e5", index:makeRotateIdx},
     {iters:lil_iters, source:"2e7", output:"4e5", index:makeRotateIdx},
     {iters:lil_iters, source:"2e7", output:"2e5", index:makeRotateIdx},
     {iters:lil_iters, source:"2e7", output:"1e5", index:makeRotateIdx},

     {iters:mid_iters, source:"1e5", output:"1e5", index:makeRotateIdx},
     {iters:mid_iters, source:"2e5", output:"2e5", index:makeRotateIdx},
     {iters:mid_iters, source:"4e5", output:"4e5", index:makeRotateIdx},
     {iters:mid_iters, source:"8e5", output:"8e5", index:makeRotateIdx},
     {iters:lil_iters, source:"1e6", output:"1e6", index:makeRotateIdx},
     {iters:lil_iters, source:"2e6", output:"2e6", index:makeRotateIdx},
     {iters:lil_iters, source:"4e6", output:"4e6", index:makeRotateIdx},
     {iters:lil_iters, source:"8e6", output:"8e6", index:makeRotateIdx},

     {iters:lil_iters, source:"8e7", output:"7e7", index:makeRotateIdx},
     {iters:lil_iters, source:"8e7", output:"6e7", index:makeRotateIdx},
     {iters:lil_iters, source:"8e7", output:"5e7", index:makeRotateIdx},
     {iters:lil_iters, source:"8e7", output:"4e7", index:makeRotateIdx},
     {iters:lil_iters, source:"8e7", output:"2e7", index:makeRotateIdx},
     {iters:lil_iters, source:"8e7", output:"1e7", index:makeRotateIdx},
     {iters:lil_iters, source:"8e7", output:"8e6", index:makeRotateIdx},
     {iters:lil_iters, source:"8e7", output:"4e6", index:makeRotateIdx},
     {iters:lil_iters, source:"8e7", output:"2e6", index:makeRotateIdx},
     {iters:lil_iters, source:"8e7", output:"1e6", index:makeRotateIdx},

     {iters:lil_iters, source:"8e6", output:"7e6", index:makeRotateIdx},
     {iters:lil_iters, source:"8e6", output:"6e6", index:makeRotateIdx},
     {iters:lil_iters, source:"8e6", output:"5e6", index:makeRotateIdx},
     {iters:lil_iters, source:"8e6", output:"4e6", index:makeRotateIdx},
     {iters:lil_iters, source:"8e6", output:"2e6", index:makeRotateIdx},
     {iters:lil_iters, source:"8e6", output:"1e6", index:makeRotateIdx},
     {iters:lil_iters, source:"8e6", output:"8e5", index:makeRotateIdx},
     {iters:lil_iters, source:"8e6", output:"4e5", index:makeRotateIdx},
     {iters:lil_iters, source:"8e6", output:"2e5", index:makeRotateIdx},
     {iters:lil_iters, source:"8e6", output:"1e5", index:makeRotateIdx},

     {iters:lil_iters, source:"8e5", output:"7e5", index:makeRotateIdx},
     {iters:lil_iters, source:"8e5", output:"6e5", index:makeRotateIdx},
     {iters:lil_iters, source:"8e5", output:"5e5", index:makeRotateIdx},
     {iters:lil_iters, source:"8e5", output:"4e5", index:makeRotateIdx},
     {iters:lil_iters, source:"8e5", output:"2e5", index:makeRotateIdx},
     {iters:lil_iters, source:"8e5", output:"1e5", index:makeRotateIdx},
     {iters:lil_iters, source:"8e5", output:"8e4", index:makeRotateIdx},
     {iters:lil_iters, source:"8e5", output:"4e4", index:makeRotateIdx},
     {iters:lil_iters, source:"8e5", output:"2e4", index:makeRotateIdx},
     {iters:lil_iters, source:"8e5", output:"1e4", index:makeRotateIdx}

    ];

  for (var k = 0; k < inputs.length; k++)
  {
    var source = inputs[k].source;
    var s = Number(source);
    var p = new ParallelArray(s, id);
    var len = ((inputs[k].index == makeIdentityIdx)
               ? source : inputs[k].output);
    var l = Number(len);
    var idx = inputs[k].index(s, l);
    var collide = plus;
    var iters = inputs[k].iters;
    var outputs = [];
    var gcCountBefore;
    var gcCountAfter;
    var seqOutput = p.scatter(idx, 0, collide, l, {mode:"seq", expect:"any"});
    var seqTime;
    for (var j = 0; j < modes.length; j++)
    {
      gc();
      gcCountBefore = gcparam("gcNumber");
      start = Date.now();
      var mode = modes[j].mode;
      var strategy = modes[j].strategy;
      if (strategy !== "") { strategy = "divide-"+strategy; }
      var r;
      for (var i = 0; i < iters; i++) {
        r = p.scatter(idx, 0, collide, l,
                      {mode:mode, strategy:strategy, expect:"any"});
      }
      outputs[j] = r;
      stop = Date.now();
      gcCountAfter = gcparam("gcNumber");

      var myTime = stop - start;
      if (j == 0)
	seqTime = myTime;

      if (false && seqOutput.length < 1000 && outputs[j].length < 1000) {
        print("  Compare outputs "+0+": "+ seqOutput);
        print("  Compare outputs "+j+": "+ outputs[j]);
      }

      print("k:"+fillstr(3, " ", k, "")+
            " s:("+fillstr(3, " ", source, "")+"->"+fillstr(3, "", len, " ")+")*"+fillstr(4, "", inputs[k].iters, " ")+
            " mode:"+mode+fillstr(23, "", strategy==""?"":"/"+strategy, " ")+
            " time:"+fillstr(6, " ", myTime, "")+
	    " "+fillstr(6, "", "("+Math.floor(100*seqTime / myTime)/100+")", " ")+" "+
	    " GCs:"+(gcCountAfter - gcCountBefore));
      assertEqParallelArray(seqOutput, outputs[j]);
    }
    print();
  }
}
