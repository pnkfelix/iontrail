load("lib/" + "parallelarray-helpers.js");

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
  var modes = [// {mode:"seq", strategy:""},
               {mode:"par", strategy:"scatter-vector"},
               {mode:"par", strategy:"output-range"}];

  function id(x) { return x; }
  function plus(x,y) { return x + y; }

  function makeIdentityIdx(sourceLen, outputLen) {
    return build(sourceLen, id);
  }

  function makeRotateIdx(sourceLen, outputLen) {
    return build(sourceLen, function(x) { return x % outputLen; });
  }

  function makeReduceIdx(sourceLen, outputLen) {
    var factor = (sourceLen / outputLen) | 0;
    return build(sourceLen, function(x) { return (x / factor) | 0});
  }

  var inputs =
    [// {iters:1,    source:10,     output:10,     index:makeIdentityIdx},
     {iters:1000,  source:10,     output:10,     index:makeIdentityIdx},
     {iters:1,    source:100,    output:100,    index:makeIdentityIdx},
     {iters:1000, source:100,    output:100,    index:makeIdentityIdx},
     {iters:1000, source:100,    output:20,     index:makeRotateIdx},
     {iters:1000, source:100,    output:20,     index:makeReduceIdx},
     {iters:1000, source:100,    output:4,      index:makeRotateIdx},
     {iters:1000, source:100,    output:4,      index:makeReduceIdx},
     {iters:100,  source:100000, output:100000, index:makeIdentityIdx},
     {iters:1000, source:100000, output:20,     index:makeRotateIdx},
     {iters:1000, source:100000, output:20,     index:makeReduceIdx},
     {iters:1000, source:100000, output:4,      index:makeRotateIdx},
     {iters:1000, source:100000, output:4,      index:makeReduceIdx}
    ];

  for (var k = 0; k < inputs.length; k++)
  {
    var p = new ParallelArray(inputs[k].source, id);
    var len = inputs[k].output;
    var idx = inputs[k].index(inputs[k].source, len);
    var collide = plus;
    var iters = inputs[k].iters;
    var outputs = [];
    for (var j = 0; j < modes.length; j++)
    {
      start = Date.now();
      var mode = modes[j].mode;
      var strategy = modes[j].strategy;
      if (strategy !== "") { strategy = "divide-"+strategy; }
      var r;
      for (var i = 0; i < iters; i++) {
        r = p.scatter(idx, 0, collide, len,
                      {mode:mode, strategy:strategy, expect:"any"});
      }
      outputs[j] = r;
      stop = Date.now();

      print("  Compare outputs "+0+": "+ outputs[0]);
      print("  Compare outputs "+j+": "+ outputs[j]);
      print("input:"+fillstr(3, " ", k, "")+
            " mode:"+mode+fillstr(23, "", strategy==""?"":"/"+strategy, " ")+
            " time:"+fillstr(6, " ", stop-start, ""));
      assertEqParallelArray(outputs[0], outputs[j]);
    }
  }
}
