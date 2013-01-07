load(libdir + "parallelarray-helpers.js");

// Test specific scatter implementation strategies, and compare them
// each against the sequential version.
//
// This is a reverse permutation of the input with collisions at front and end
// [A, B, C, D, ..., W, X, Y, Z] ==> [Z+Y, X, W, ..., D, C, B+A]

function testDivideScatterVector() {
    var len = 1024;
    function add1(x) { return x+1; }
    function add3(x) { return x+3; }
    function id(x) { return x; }
    var p = new ParallelArray(len, add1);
    var idx = [0,0].concat(build(len-4, add1)).concat([len-3,len-3]);
    var revidx = idx.reverse();
    var p2 = [3].concat(build(len-4, add3)).concat([2*len-1]);
    var expect = new ParallelArray(p2.reverse());
    var modes = [["success", "seq", ""],
                 ["success", "par", "divide-scatter-vector", "merge:seq"],
                 ["mixed",   "par", "divide-scatter-vector", "merge:unset"],
                 ["mixed",   "par", "divide-scatter-vector", "merge:par"],
                 ["success", "par", "divide-output-range"]];
    for (var i = 0; i < modes.length; i++) {
        print(modes[i].slice(2));
        var m = {mode: modes[i][1], strategy: modes[i][2], expect: modes[i][0]};
        if (modes[i][2] == "merge:par") m.merge = "par";
        else if (modes[i][2] == "merge:seq") m.merge = "seq";
        var r = p.scatter(revidx, 0, function (x,y) { return x+y; }, len-2, m);
        assertEqParallelArray(r, expect);
    }
}

testDivideScatterVector();
