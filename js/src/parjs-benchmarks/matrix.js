  // Assumption: if MODE defined then running under benchmark script
var benchmarking = (typeof(MODE) != "undefined");

// Assumption: if libdir undefined then it is current directory (but this one we warn about)
if (typeof(libdir) == "undefined") {
  print("Selecting default libdir of './';");
  print("you should override if you are not running from current directory.");
  var libdir = "./";
}

if (benchmarking) {
  // util.js provides interface for benchmark infrastructure.
  load(libdir + "util.js");
}

function kernel_core(x,y) {
  return x*1000 + y;
}

var W = 500;
var H = 5000;

function kernel_fresh(x) {
  // print("kernel_fresh");
  return new ParallelMatrix([H], function (y) kernel_core(x,y), mode);
}

function kernel_token(x, tok) {
  // print("kernel_token");
  return new ParallelMatrix(tok, function (y) kernel_core(x,y), mode);
}

function myprint(x) {
  if (x instanceof String) {
    print(x);
  } else {
    print(JSON.stringify(x,
                         function (k, x) {
                           if (x instanceof Array) {
                             return x.slice(0, Math.min(x.length,
                                                        4));
                           } else {
                             return x;
                           }
                         },
                         4));
  }
}
var seq_mode = {mode:"seq"
                 //, print:myprint
               };
var par_mode = {mode:"par"
                 //, print:myprint
               };

var mode;

function computeSeqFresh() {
  // print("computeSeqFresh");
  mode = seq_mode;
  return new ParallelMatrix([W,H], [H], kernel_fresh, mode);
}

function computeSeqToken() {
  // print("computeSeqToken");
  mode = seq_mode;
  return new ParallelMatrix([W,H], [H], kernel_token, mode);
}

function computeParFresh() {
  // print("computeParFresh");
  mode = par_mode;
  return new ParallelMatrix([W,H], [H], kernel_fresh, mode);
}

function computeParToken() {
  // print("computeParToken");
  mode = par_mode;
  return new ParallelMatrix([W,H], [H], kernel_token, mode);
}

benchmark("MATRIX-SEQ-FRESH-VS-TOKEN", 1, DEFAULT_MEASURE,
          computeSeqFresh, computeSeqToken);

benchmark("MATRIX-FRESH-SEQ-VS-PAR", 1, DEFAULT_MEASURE,
          computeSeqFresh, computeParFresh);

benchmark("MATRIX-TOKEN-SEQ-VS-PAR", 1, DEFAULT_MEASURE,
          computeSeqToken, computeParToken);
