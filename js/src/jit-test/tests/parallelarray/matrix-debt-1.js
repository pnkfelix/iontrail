ParallelMatrixDebt.prototype.toSource =
  function toSource() {
    var ret="({escrow ";
    for (var i = 0; i < this.length; i++) {
      ret += this.get(i);
      if (i+1 < this.length)
        ret += ", ";
    }
    ret+="})";
    return ret;
  };

function viewToSource2d(view, width, height, payload) {
  var i=0;
  var ret = "[";
  var matrixNeedsNewline = false;
  for (var row=0; row < height; row++) {
    if (matrixNeedsNewline)
      ret += ",\n ";
    ret += "[";
    var rowNeedsComma = false;
    for (var x=0; x < width; x++) {
      if (rowNeedsComma)
        ret += ", ";
      if (payload == 1) {
        var val = view(i);
        if (val !== undefined)
          ret += val;
        i++;
      } else {
        var entryNeedsComma = false;
        ret += "(";
        for (var k=0; k < payload; k++) {
          // Might be inefficient (does JavaScript have
          // StringBuffers?, or use them internally, like Tamarin?)
          if (entryNeedsComma)
            ret += ", ";
          var val = view(i);
          if (val !== undefined)
            ret += val;
          entryNeedsComma = true;
          i++;
        }
        ret += ")";
      }
      rowNeedsComma = true;
    }
    ret += "]";
    matrixNeedsNewline = true;
  }
  ret += "]";
  return ret;
}

function dbprint(x) {
  // print(x);
}

ParallelMatrix.prototype.toSource =
  function toSource() {
    var self = this;
    var slen = self.shape.length;
    if (slen == 1) {
      return "[" + this.buffer.join(",") + "]";
    } else {
      var w = self.shape[0];
      var h = self.shape[1];
      var p = 1;
      for (var i = 2; i < slen; i++) {
        p *= self.shape[i];
      }
      return viewToSource2d(function (j) { dbprint("view("+j+")"); return self.buffer[self.offset+j];}, w, h, p );
    }
  };

function C(shape, builder) {
  this.shape = shape;
  this.builder = builder;
  for (var i = 0; i < shape; i++) {
    this[i] = builder(i);
  }
}

C.prototype.map1 = function map(_f) {
  return new C(this.shape, function (_i) {
                 return _f(this[_i], _i, this);
               });
};

C.prototype.map2 = function map(_f) {
  var self = this;
  return new C(this.shape, function (_i) {
                 return _f(self[_i], _i, self);
               });
};

// Explicitly initializes the array to force it into a Dense state.
function NewFilledArray(len) {
  var buffer = new Array(10);
  for(var i=0; i < buffer.length; i++) buffer[i] = undefined;
  return buffer;
}

var buffer = NewFilledArray(10);
var d1 =  new ParallelMatrixDebt([5], buffer, 0);
var d2 =  new ParallelMatrixDebt([5], buffer, 5);
function fill(j, context) {
  return function (i) {
    dbprint("callback for "+context+" i:"+i+" j:"+j); j = j ? j : 0;
    return 100+i*10+j;
  };}
var pm1 =  new ParallelMatrix(d1, fill(1, "pm1"));
var pm2 =  new ParallelMatrix(d2, fill(2, "pm2"));
var pm3 =  new ParallelMatrix([5], fill(3, "pm3"));

var pm4 =  new ParallelMatrix([5,5], function(i,j) (100+i*10+j));
