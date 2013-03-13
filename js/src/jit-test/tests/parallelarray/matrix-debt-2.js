load(libdir + "parallelarray-helpers.js");

function cell(...args) {
  var ret = 0;
  var d;
  while ((d = args.shift()) != undefined) {
    ret *= 10;
    ret += d+1;
  }
  return ret;
}

function test_2d() {
  var pm2d = null;
  var exceptionThrown = false;
  try {
    pm2d = new ParallelMatrix([5,6], [6],
             function(i,t) {
               return new ParallelMatrix([6], function (j) cell(i,j));
             });
  } catch (e) {
    exceptionThrown = true;
  }

  assertEq(pm2d, null);
  assertEq(exceptionThrown, true);
}

test_2d();
