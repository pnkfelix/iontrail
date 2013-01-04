load(libdir + "parallelarray-helpers.js");

// Test specific scatter implementation strategies, and compare them
// each against the sequential version.
//
// This is just a simple reverse permutation of the input:
// [A, B, ..., Y, Z] ==> [Z, Y, ..., B, A]

function testDivideScatterVector() {
    var len = 15;
    function add1(x) { return x+1; }
    function id(x) { return x; }
    var p = new ParallelArray(len, add1);
    var revidx = build(len, id).reverse();
    var p2 = new ParallelArray(revidx.map(add1));
    var modes = [["seq", ""],
                 ["par", "divide-scatter-vector", "merge:seq"],
                 // ["par", "divide-scatter-vector", "merge:unset"],
                 // ["par", "divide-scatter-vector", "merge:par"],
                 ["par", "divide-output-range"]];
    for (var i = 0; i < modes.length; i++) {
      print(modes[i].slice(1));
        var m = {mode: modes[i][0], strategy: modes[i][1],
                 expect: "success", merge: "par"};
        if (modes[i][2] == "merge:par") m.merge = "par";
        else if (modes[i][2] == "merge:seq") m.merge = "seq";
        m.print = function(x) { print(JSON.stringify(x)); };
        var r = p.scatter(revidx, 0, undefined, len, m);
        assertEqParallelArray(r, p2);
    }
}

testDivideScatterVector();
