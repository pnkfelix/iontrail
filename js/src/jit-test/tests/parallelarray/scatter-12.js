load(libdir + "parallelarray-helpers.js");

// Test specific scatter implementation strategies, and compare them
// each against the sequential version.
//
// This is a reverse permutation that has a gap at the start and at the end.
// [A, B, ..., Y, Z] ==> [0, Z, Y, ..., B, A, 0]

function testDivideScatterVector() {
    var len = 1024;
    function add1(x) { return x+1; }
    function id(x) { return x; }
    var p = new ParallelArray(len, add1);
    var revidx = build(len, add1).reverse();
    var p2 = new ParallelArray([0].concat(revidx).concat([0]));
    var modes = [["seq", ""],
                 ["par", "divide-scatter-vector", "merge:seq"],
                 ["par", "divide-scatter-vector", "merge:unset"],
                 ["par", "divide-scatter-vector", "merge:par"],
                 ["par", "divide-output-range"]];
    for (var i = 0; i < modes.length; i++) {
        var m = {mode: modes[i][0], strategy: modes[i][1], expect: "success"};
        if (modes[i][2] == "merge:par") m.merge = "par";
        else if (modes[i][2] == "merge:seq") m.merge = "seq";
        var r = p.scatter(revidx, 0, undefined, len+2, m);
        assertEqParallelArray(r, p2);
    }
}

testDivideScatterVector();
