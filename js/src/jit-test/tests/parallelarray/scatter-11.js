load(libdir + "parallelarray-helpers.js");

// Test specific scatter implementation strategies, and compare them
// each against the sequential version.
//
// This is a reverse permutation that has a gap at the end.
// [A, B, ..., Y, Z] ==> [Z, Y, ..., B, A, 0]

function testDivideScatterVector() {
    var len = minItemsTestingThreshold;
    function add1(x) { return x+1; }
    function id(x) { return x; }
    var p = new ParallelArray(len, add1);
    var revidx = build(len, id).reverse();
    var p2 = new ParallelArray(revidx.map(add1).concat([0]));
    var modes = [["success", "seq", "", ""],
                 ["success", "par", "divide-scatter-vector", "merge:seq"],
                 ["mixed",   "par", "divide-scatter-vector", "merge:unset"],
                 ["mixed",   "par", "divide-scatter-vector", "merge:par"],
                 ["success", "par", "divide-output-range", ""]];
    for (var i = 0; i < modes.length; i++) {
        print(modes[i].slice(2));
        var m = {mode: modes[i][1], strategy: modes[i][2], expect: modes[i][0]};
        if (modes[i][3] == "merge:par") m.merge = "par";
        if (modes[i][3] == "merge:seq") m.merge = "seq";
        // m.print = function(x) { print(JSON.stringify(x)); };
        var r = p.scatter(revidx, 0, undefined, len+1, m);
        assertEqParallelArray(r, p2);
    }
}

testDivideScatterVector();
