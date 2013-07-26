load(libdir + "parallelarray-helpers.js");

function buildSimple() {

    assertParallelModesCommute(function(m) {
        return Array.buildPar(256, function(i) {
            return [i, i+1, i+2, i+3];
        }, m);
    });

    assertParallelModesCommute(function(m) {
        return Array.buildPar(256, function(i) {
            var x = [];
            for (var i = 0; i < 4; i++) {
                x[i] = i;
            }
            return x;
        }, m);
    });

    assertParallelModesCommute(function(m) {
        return Array.buildPar(256, function(i) {
            var x = [];
            for (var i = 0; i < 99; i++) {
                x[i] = i;
            }
            return x;
        }, m);
    });
}

if (getBuildConfiguration().parallelJS)
  buildSimple();
