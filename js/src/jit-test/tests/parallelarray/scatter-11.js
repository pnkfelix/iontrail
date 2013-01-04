load(libdir + "parallelarray-helpers.js");

// Test specific scatter implementation strategies, and compare them
// each against the sequential version.
//
// This is a reverse permutation that has a gap at the end.
// [A, B, ..., Y, Z] ==> [Z, Y, ..., B, A, 0]

function testDivideScatterVector() {
    var len = 13;
    function add1(x) { return x+1; }
    function id(x) { return x; }
    var p = new ParallelArray(len, add1);
    var revidx = build(len, id).reverse();
    var p2 = new ParallelArray(revidx.map(add1).concat([0]));
    var modes = [["seq", "", ""],
                 ["par", "divide-scatter-vector", "merge:seq"],
                 ["par", "divide-scatter-vector", "merge:unset"],
                 ["par", "divide-scatter-vector", "merge:par"],
                 ["par", "divide-output-range", ""]];
    for (var i = 0; i < modes.length; i++) {
        var m = {mode: modes[i][0], strategy: modes[i][1], expect: "success"};
        // if (modes[i][2] == "merge:par") m.merge = "par";
        // if (modes[i][2] == "merge:seq") m.merge = "seq";
        m.print = function(x) { print(JSON.stringify(x)); };
        var r = p.scatter(revidx, 0, undefined, len+1, m);
      print(modes[i].slice(1), r.buffer);
        assertEqParallelArray(r, p2);
    }
}

testDivideScatterVector();
