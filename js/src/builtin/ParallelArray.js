// FIXME: ICs must work in parallel.
// FIXME: Must be able to call native intrinsics in parallel with a JSContext,
//        or have native intrinsics provide both a sequential and a parallel
//        version.
// TODO: Use let over var when Ion compiles let.
// TODO: Private names.
// XXX: Hide buffer and other fields?

function ComputeTileBounds(len, id, n) {
  var slice = (len / n) | 0;
  var start = slice * id;
  var end = id === n - 1 ? len : slice * (id + 1);
  return [start, end];
}

function TruncateEnd(start, end) {
  var end1 = start + 3;
  if (end1 < end) return end1;
  return end;
}

function ComputeProducts(shape) {
  // Compute the partial products in reverse order.
  // e.g., if the shape is [A,B,C,D], then the
  // array |products| will be [1,D,CD,BCD].
  var product = 1;
  var products = [];
  var sdimensionality = shape.length;
  for (var i = sdimensionality - 1; i >= 0; i--) {
    products.push(product);
    product *= shape[i];
  }
  return products;
}

function ComputeIndices(shape, index1d) {
  // Given a shape and some index |index1d|, computes and returns an
  // array containing the N-dimensional index that maps to |index1d|.

  var products = ComputeProducts(shape);
  var l = shape.length;

  var result = [];
  for (var i = 0; i < l; i++) {
    // Obtain product of all higher dimensions.
    // So if i == 0 and shape is [A,B,C,D], yields BCD.
    var stride = products[l - i - 1];

    // Compute how many steps of width stride we could take.
    var index = (index1d / stride) | 0;
    result[i] = index;

    // Adjust remaining indices for smaller dimensions.
    index1d -= (index * stride);
  }

  return result;
}

function StepIndices(shape, indices) {
  var i = shape.length - 1;
  while (i >= 0) {
    var indexi = indices[i] + 1;
    if (indexi < shape[i]) {
      indices[i] = indexi;
      return;
    }
    indices[i] = 0;
    i--;
  }
}

function IsInteger(v) {
  return (v | 0) === v;
}

// Constructor
//
// We split the 3 construction cases so that we don't case on arguments.

function ParallelArrayConstruct0() {
  this.buffer = [];
  this.offset = 0;
  this.shape = [0];
  this.get = ParallelArrayGet1;
}

function ParallelArrayConstruct1(buffer) {
  var buffer = %ToObject(buffer);
  var length = buffer.length >>> 0;
  if (length !== buffer.length)
    %ThrowError(JSMSG_PAR_ARRAY_BAD_ARG, "");

  var buffer1 = [];
  for (var i = 0; i < length; i++)
    buffer1[i] = buffer[i];

  this.buffer = buffer1;
  this.offset = 0;
  this.shape = [length];
  this.get = ParallelArrayGet1;
}

function ParallelArrayConstruct2(shape, f, bufferSpec) {
  if (typeof shape === "number") {
    var length = shape >>> 0;
    if (length !== shape)
      %ThrowError(JSMSG_PAR_ARRAY_BAD_ARG, "");
    ParallelArrayBuild(this, [length], f, undefined, bufferSpec);
  } else {
    var shape1 = [];
    for (var i = 0, l = shape.length; i < l; i++) {
      var s0 = shape[i];
      var s1 = s0 >>> 0;
      if (s1 !== s0)
        %ThrowError(JSMSG_PAR_ARRAY_BAD_ARG, "");
      shape1[i] = s1;
    }
    ParallelArrayBuild(this, shape1, f, bufferSpec);
  }
}

// We duplicate code here to avoid extra cloning.
function ParallelArrayConstruct3(shape, f, m) {
  if (typeof shape === "number") {
    var length = shape >>> 0;
    if (length !== shape)
      %ThrowError(JSMSG_PAR_ARRAY_BAD_ARG, "");
    ParallelArrayBuild(this, [length], f, m);
  } else {
    var shape1 = [];
    for (var i = 0, l = shape.length; i < l; i++) {
      var s0 = shape[i];
      var s1 = s0 >>> 0;
      if (s1 !== s0)
        %ThrowError(JSMSG_PAR_ARRAY_BAD_ARG, "");
      shape1[i] = s1;
    }
    ParallelArrayBuild(this, shape1, f, m);
  }
}

function ParallelArrayView(shape, buffer, offset) {
  this.shape = shape;
  this.buffer = buffer;
  this.offset = offset;

  switch (shape.length) {
    case 1: this.get = ParallelArrayGet1; break;
    case 2: this.get = ParallelArrayGet2; break;
    case 3: this.get = ParallelArrayGet3; break;
    default: this.get = ParallelArrayGetN; break;
  }
}

function ParallelArrayBuild(self, shape, f, m, bufferSpec) {
  self.shape = (typeof(bufferSpec) === 'object') ? shape.concat(bufferSpec.shape) : shape;
  self.offset = 0;

  var writeOp = (typeof(bufferSpec) === 'object') ? bufferSpec.write : function (b, i, v, f) { f(b, i, v); };
  var length;
  var xw, yw, zw;
  var fill;

  switch (shape.length) {
  case 1:
    length = shape[0];
    self.get = ParallelArrayGet1;
    fill = fill1;
    break;
  case 2:
    xw = shape[0];
    yw = shape[1];
    length = xw * yw;
    self.get = ParallelArrayGet2;
    fill = fill2;
    break;
  case 3:
    xw = shape[0];
    yw = shape[1];
    zw = shape[2];
    length = xw * yw * zw;
    self.get = ParallelArrayGet3;
    fill = fill3;
    break;
  default:
    length = 1;
    for (var i = 0; i < shape.length; i++)
      length *= shape[i];
    self.get = ParallelArrayGetN;
    fill = fillN;
    break;
  }

  var done = false;
  var buffer = (typeof(bufferSpec) === 'object') ? bufferSpec.construct(length) : %DenseArray(length);

  if (TRY_PARALLEL(m) && %EnterParallelSection()) {
    // FIXME: Just throw away some work for now to warmup every time.
    fill(0, 1, true, yw, zw, writeOp);
    done = %ParallelDo(fill, CheckParallel(m), false, yw, zw, writeOp);
    %LeaveParallelSection();
  }

  if (!done && TRY_SEQUENTIAL(m)) {
    fill(0, 1, false, yw, zw, writeOp);
    done = true;
  }

  if (done) {
    self.shape = shape;
    self.buffer = buffer;
    return;
  }

  var emptyShape = [];
  for (var i = 0; i < shape.length; i++)
    emptyShape[i] = 0;
  self.shape = emptyShape;
  self.buffer = [];

  function fill1(id, n, warmup, yw, zw, writeOp) {
    var [start, end] = ComputeTileBounds(length, id, n);
    if (warmup)
      end = TruncateEnd(start, end);
    for (var i = start; i < end; i++)
      writeOp(buffer, i, f(i), function (b,i,v) { %UnsafeSetElement(b, i, v); });
  }

  function fill2(id, n, warmup, yw, zw, writeOp) {
    var [start, end] = ComputeTileBounds(length, id, n);
    if (warmup)
      end = TruncateEnd(start, end);
    var x = (start / yw) | 0;
    var y = start - x*yw;
    for (var i = start; i < end; i++) {
      writeOp(buffer, i, f(x, y), function (b,i,v) { %UnsafeSetElement(b, i, v); });
      if (++y == yw) {
        y = 0;
        ++x;
      }
    }
  }

  function fill3(id, n, warmup, yw, zw, writeOp) {
    var [start, end] = ComputeTileBounds(length, id, n);
    if (warmup)
      end = TruncateEnd(start, end);
    var x = (start / (yw*zw)) | 0;
    var r = start - x*yw*zw;
    var y = (r / zw) | 0;
    var z = r - y*zw;
    for (var i = start; i < end; i++) {
      writeOp(buffer, i, f(x, y, z), function (b,i,v) { %UnsafeSetElement(b, i, v); });
      if (++z == zw) {
        z = 0;
        if (++y == yw) {
          y = 0;
          ++x;
        }
      }
    }
  }

  function fillN(id, n, warmup, yw, zw, writeOp) {
    // NB: In fact this will not currently be parallelized due to the
    // use of `f.apply()`.  But it's written as if it could be.  A guy
    // can dream, can't he?
    var [start, end] = ComputeTileBounds(length, id, n);
    if (warmup)
      end = TruncateEnd(start, end);
    var indices = ComputeIndices(shape, start);
    for (var i = start; i < end; i++) {
      writeOp(buffer, i, f.apply(null, indices), function (b,i,v) { %UnsafeSetElement(b, i, v); });
      StepIndices(shape, indices);
    }
  }
}

function ParallelArrayMap(f, m) {
  var self = this;
  var length = self.shape[0];

  ///////////////////////////////////////////////////////////////////////////
  // Parallel

  var buffer = %DenseArray(length);

  // Note: at the moment, writing "if (%InParallelSection() &&
  // TRY_PARALLEL(m))" is not fully optimized away.  This would require
  // repeated loops to get it right, or else perhaps integrating UCE
  // and GVN.
  if (TRY_PARALLEL(m)) {
    if (%EnterParallelSection()) {
      // FIXME: Just throw away some work for now to warmup every time.
      fill(0, 1, true);

      if (%ParallelDo(fill, CheckParallel(m), false)) {
        %LeaveParallelSection();
        return %NewParallelArray(ParallelArrayView, [length], buffer, 0);
      }

      %LeaveParallelSection();
    }
  }

  ///////////////////////////////////////////////////////////////////////////
  // Sequential

  if (TRY_SEQUENTIAL(m)) {
    fill(0, 1, false);
    return %NewParallelArray(ParallelArrayView, [length], buffer, 0);
  }

  return %NewParallelArray(ParallelArrayView, [0], [], 0);

  function fill(id, n, warmup) {
    var [start, end] = ComputeTileBounds(length, id, n);
    if (warmup)
      end = TruncateEnd(start, end);
    for (var i = start; i < end; i++)
      %UnsafeSetElement(buffer, i, f(self.get(i), i, self));
  }
}

function ParallelArrayReduce(f, m) {
  var self = this;
  var length = self.shape[0];

  if (length === 0)
    %ThrowError(JSMSG_PAR_ARRAY_REDUCE_EMPTY);

  ///////////////////////////////////////////////////////////////////////////
  // Parallel Version

  if (TRY_PARALLEL(m) && %EnterParallelSection()) {
    var slices = %ParallelSlices();
    if (length > slices) {
      // Attempt parallel reduction, but only if there is at least one
      // element per thread.  Otherwise the various slices having to
      // reduce empty spans of the source array.
      var subreductions = %DenseArray(slices);

      // FIXME: Just throw away some work for now to warmup every time.
      fill(0, 1, true);

      if (%ParallelDo(fill, CheckParallel(m), false)) {
        // can't use reduce because subreductions is an array, not a
        // parallel array:
        var a = subreductions[0];
        for (var i = 1; i < subreductions.length; i++)
          a = f(a, subreductions[i]);
        %LeaveParallelSection();
        return a;
      }
    }
    %LeaveParallelSection();
  }

  ///////////////////////////////////////////////////////////////////////////
  // Sequential Version

  if (TRY_SEQUENTIAL(m)) {
    return reduce(0, length);
  }

  return self.get(0);

  ///////////////////////////////////////////////////////////////////////////
  // Helpers

  function reduce(start, end) {
    // The accumulator: the objet petit a.
    //
    // "A VM's accumulator register is Objet petit a: the unattainable object
    // of desire that sets in motion the symbolic movement of interpretation."
    //     -- PLT Zizek
    var a = self.get(start);
    for (var i = start+1; i < end; i++)
      a = f(a, self.get(i));
    return a;
  }

  function fill(id, n, warmup) {
    var [start, end] = ComputeTileBounds(length, id, n);
    if (warmup)
      end = TruncateEnd(start, end);
    %UnsafeSetElement(subreductions, id, reduce(start, end));
  }
}

function ParallelArrayScan(f, m) {
  var self = this;
  var length = self.shape[0];

  if (length === 0)
    %ThrowError(JSMSG_PAR_ARRAY_REDUCE_EMPTY);

  ///////////////////////////////////////////////////////////////////////////
  // Parallel version

  var buffer = %DenseArray(length);

  if (TRY_PARALLEL(m) && %EnterParallelSection()) {
    var slices = %ParallelSlices();
    if (length > slices) { // Each worker thread will have something to do.
      // FIXME: Just throw away some work for now to warmup every time.
      phase1(0, 1, true);

      // compute scan of each slice: see comment on phase1() below
      if (%ParallelDo(phase1, CheckParallel(m), false)) {
        // build intermediate array: see comment on phase2() below
        var intermediates = [];
        var [start, end] = ComputeTileBounds(length, 0, slices);
        var acc = phase1buffer[end-1];
        intermediates[0] = acc;
        for (var i = 1; i < slices - 1; i++) {
          [start, end] = ComputeTileBounds(length, i, slices);
          acc = f(acc, phase1buffer[end-1]);
          intermediates[i] = acc;
        }

        // FIXME: Just throw away some work for now to warmup every time.
        phase2(0, 1, true);

        // compute phase1 scan results with intermediates
        if (%ParallelDo(phase2, CheckParallel(m), false)) {
          %LeaveParallelSection();
          return %NewParallelArray(ParallelArrayView, [length], buffer, 0);
        }
      }
    }

    %LeaveParallelSection();
  }

  ///////////////////////////////////////////////////////////////////////////
  // Sequential version

  if (TRY_SEQUENTIAL(m)) {
    scan(0, length);
    return %NewParallelArray(ParallelArrayView, [length], buffer, 0);
  }

  return %NewParallelArray(ParallelArrayView, [0], [], 0);

  ///////////////////////////////////////////////////////////////////////////
  // Helpers

  function scan(start, end) {
    var acc = self.get(0);
    buffer[start] = acc;
    for (var i = start + 1; i < end; i++) {
      acc = f(acc, self.get(i));
      %UnsafeSetElement(buffer, i, acc);
    }
  }

  function phase1(id, n, warmup) {
    // In phase 1, we divide the source array into n even slices and
    // compute scan on each slice sequentially as it were the entire
    // array.  This function is responsible for computing one of those
    // slices.
    //
    // So, if we have an array [A,B,C,D,E,F,G,H,I], n == 3, and our function
    // |f| is sum, then would wind up computing a result array like:
    //
    //     [A, A+B, A+B+C, D, D+E, D+E+F, G, G+H, G+H+I]
    //      ^~~~~~~~~~~~^  ^~~~~~~~~~~~^  ^~~~~~~~~~~~~^
    //      Slice 0        Slice 1        Slice 2
    //
    // Read on in phase2 to see what we do next!
    var [start, end] = ComputeTileBounds(self.shape[0], id, n);
    if (warmup)
      end = TruncateEnd(start, end);
    scan(start, end);
  }

  function phase2(id, n, warmup) {
    // After computing the phase1 results, we compute an
    // |intermediates| array.  |intermediates[i]| contains the result
    // of reducing the final value from each preceding slice j<i with
    // the final value of slice i.  So, to continue our previous
    // example, the intermediates array would contain:
    //
    //   [A+B+C, (A+B+C)+(D+E+F), ((A+B+C)+(D+E+F))+(G+H+I)]
    //
    // Here I have used parenthesization to make clear the order of
    // evaluation in each case.
    //
    //   An aside: currently the intermediates array is computed
    //   sequentially.  In principle, we could compute it in parallel,
    //   at the cost of doing duplicate work.  This did not seem
    //   particularly advantageous to me, particularly as the number
    //   of slices is typically quite small (one per core), so I opted
    //   to just compute it sequentially.
    //
    // Phase 2 combines the results of phase1 with the intermediates
    // array to produce the final scan results.  The idea is to
    // reiterate over each element S[i] in the slice |id|, which
    // currently contains the result of reducing with S[0]...S[i]
    // (where S0 is the first thing in the slice), and combine that
    // with |intermediate[id-1]|, which represents the result of
    // reducing everything in the input array prior to the slice.
    //
    // To continue with our example, in phase 1 we computed slice 1 to
    // be [D, D+E, D+E+F].  We will combine those results with
    // |intermediates[1-1]|, which is |A+B+C|, so that the final
    // result is [(A+B+C)+D, (A+B+C)+(D+E), (A+B+C)+(D+E+F)].  Again I
    // am using parentheses to clarify how these results were reduced.

    if (id > 0) { // The 0th worker has nothing to do.
      var [start, end] = ComputeTileBounds(self.shape[0], id, n);
      if (warmup) { end = TruncateEnd(start, end); }
      var intermediate = intermediates[id - 1];
      for (var i = start; i < end; i++)
        %UnsafeSetElement(buffer, i, f(intermediate, buffer[i]));
    }
  }
}

function ParallelArrayScatter(targets, zero, f, length) {
  // TODO: N-dimensional
  // TODO: Parallelize. %ThrowError or any calling of intrinsics isn't safe.
  function fill(result, id, n, targets, zero, f, source) {
    var length = result.length;

    // Initialize a conflict array and initialize the result to the zero value.
    var conflict = [];
    var [start, end] = ComputeTileBounds(length, id, n);
    for (var i = start; i < end; i++) {
      result[i] = zero;
      conflict[i] = false;
    }

    var limit = length < targets.length ? length : targets.length;
    var [start, end] = ComputeTileBounds(limit, id, n);

    for (var i = start; i < end; i++) {
      var t = targets[i];

      if (t >>> 0 !== t)
        %ThrowError(JSMSG_PAR_ARRAY_BAD_ARG, ".prototype.scatter");

      if (t >= length)
        %ThrowError(JSMSG_PAR_ARRAY_SCATTER_BOUNDS);

      if (conflict[t]) {
        if (!f)
          %ThrowError(JSMSG_PAR_ARRAY_SCATTER_CONFLICT);
        result[t] = f(source[i], result[t]);
      } else {
        result[t] = source[i];
        conflict[t] = true;
      }
    }
  }

  var source = this.buffer;

  if (targets.length >>> 0 !== targets.length)
    %ThrowError(JSMSG_BAD_ARRAY_LENGTH, "");
  if (length && length >>> 0 !== length)
    %ThrowError(JSMSG_BAD_ARRAY_LENGTH, "");

  var buffer = [];
  buffer.length = length || source.length;
  fill(buffer, 0, 1, targets, zero, f, source);

  return %NewParallelArray(ParallelArrayView, [buffer.length], buffer, 0);
}

function ParallelArrayFilter(filters, m) {
  var self = this;
  var length = self.shape[0];

  if (filters.length >>> 0 !== length)
    %ThrowError(JSMSG_BAD_ARRAY_LENGTH, "");

  ///////////////////////////////////////////////////////////////////////////
  // Parallel version

  if (TRY_PARALLEL(m) && %EnterParallelSection()) {
    var slices = %ParallelSlices();
    if (length > slices) {
      var keepers = %DenseArray(slices);

      // FIXME: Just throw away some work for now to warmup every time.
      countKeepers(0, 1, true);

      if (%ParallelDo(countKeepers, CheckParallel(m), false)) {
        var total = 0;
        for (var i = 0; i < keepers.length; i++)
          total += keepers[i];

        if (total == 0) {
          %LeaveParallelSection();
          return %NewParallelArray(ParallelArrayView, [0], [], 0);
        }

        var buffer = %DenseArray(total);

        // FIXME: Just throw away some work for now to warmup every time.
        for (var slice = 0; slice < slices; slice++)
          copyKeepers(slice, slices, true);

        if (%ParallelDo(copyKeepers, CheckParallel(m), false)) {
          %LeaveParallelSection();
          return %NewParallelArray(ParallelArrayView, [total], buffer, 0);
        }
      }
    }

    %LeaveParallelSection();
  }

  ///////////////////////////////////////////////////////////////////////////
  // Sequential version
  var buffer = [];
  if (TRY_SEQUENTIAL(m)) {
    for (var i = 0, pos = 0; i < length; i++) {
      if (filters[i])
        buffer[pos++] = self.get(i);
    }
  }
  return %NewParallelArray(ParallelArrayView, [buffer.length], buffer, 0);

  function countKeepers(id, n, warmup) {
    var [start, end] = ComputeTileBounds(length, id, n);
    if (warmup)
      end = TruncateEnd(start, end);
    var count = 0;
    for (var i = start; i < end; i++) {
      if (filters[i])
        count++;
    }
    %UnsafeSetElement(keepers, id, count);
  }

  function copyKeepers(id, n, warmup) {
    var [start, end] = ComputeTileBounds(length, id, n);
    if (warmup)
      end = TruncateEnd(start, end);

    var pos = 0;
    if (id > 0) { // FIXME(#819219)---work around a bug in Ion's range checks
      for (var i = 0; i < id; i++)
        pos += keepers[i];
    }

    for (var i = start; i < end; i++) {
      if (filters[i])
        %UnsafeSetElement(buffer, pos++, self.get(i));
    }
  }
}

function ParallelArrayPartition(amount) {
  if (amount >>> 0 !== amount)
    %ThrowError(JSMSG_BAD_ARRAY_LENGTH, ""); // XXX

  var length = this.shape[0];
  var partitions = (length / amount) | 0;

  if (partitions * amount !== length)
    %ThrowError(JSMSG_BAD_ARRAY_LENGTH, ""); // XXX

  var shape = [partitions, amount];
  for (var i = 1; i < this.shape.length; i++)
    shape.push(this.shape[i]);
  return %NewParallelArray(ParallelArrayView, shape, this.buffer, this.offset);
}

function ParallelArrayFlatten() {
  if (this.shape.length < 2)
    %ThrowError(JSMSG_BAD_ARRAY_LENGTH, ""); // XXX

  var shape = [this.shape[0] * this.shape[1]];
  for (var i = 2; i < this.shape.length; i++)
    shape.push(this.shape[i]);
  return %NewParallelArray(ParallelArrayView, shape, this.buffer, this.offset);
}

//
// Accessors and utilities.
//

function ParallelArrayGet1(i) {
  if (i === undefined)
    return undefined;
  return this.buffer[this.offset + i];
}

function ParallelArrayGet2(x, y) {
  var xw = this.shape[0];
  var yw = this.shape[1];
  if (x === undefined)
    return undefined;
  if (x >= xw)
    return undefined;
  if (y === undefined)
    return %NewParallelArray(ParallelArrayView, [yw], this.buffer, this.offset + x*yw);
  if (y >= yw)
    return undefined;
  var offset = y + x*yw;
  return this.buffer[this.offset + offset];
}

function ParallelArrayGet3(x, y, z) {
  var xw = this.shape[0];
  var yw = this.shape[1];
  var zw = this.shape[2];
  if (x === undefined)
    return undefined;
  if (x >= xw)
    return undefined;
  if (y === undefined)
    return %NewParallelArray(ParallelArrayView, [yw, zw], this.buffer, this.offset + x*yw*zw);
  if (y >= yw)
    return undefined;
  if (z === undefined)
    return %NewParallelArray(ParallelArrayView, [zw], this.buffer, this.offset + y*zw + x*yw*zw);
  if (z >= zw)
    return undefined;
  var offset = z + y*zw + x*yw*zw;
  return this.buffer[this.offset + offset];
}

function ParallelArrayGetN(...coords) {
  if (coords.length == 0)
    return undefined;

  var products = ComputeProducts(this.shape);

  // Compute the offset of the given coordinates.  Each index is
  // multipled by its corresponding entry in the |products|
  // array, counting in reverse.  So if |coords| is [a,b,c,d],
  // then you get |a*BCD + b*CD + c*D + d|.
  var offset = this.offset;
  var sdimensionality = this.shape.length;
  var cdimensionality = coords.length;
  for (var i = 0; i < cdimensionality; i++) {
    if (coords[i] >= this.shape[i])
      return undefined;
    offset += coords[i] * products[sdimensionality - i - 1];
  }

  if (cdimensionality < sdimensionality) {
    var shape = this.shape.slice(cdimensionality);
    return %NewParallelArray(ParallelArrayView, shape, this.buffer, offset);
  }
  return this.buffer[offset];
}

function ParallelArrayLength() {
  return this.shape[0];
}

function ParallelArrayToString() {
  var l = this.shape[0];
  if (l == 0)
    return "";

  var open, close;
  if (this.shape.length > 1) {
    open = "<"; close = ">";
  } else {
    open = close = "";
  }

  var result = "";
  for (var i = 0; i < l - 1; i++) {
    result += open + this.get(i).toString() + close;
    result += ",";
  }
  result += open + this.get(l-1).toString() + close;
  return result;
}

function CheckParallel(m) {
  if (!m)
    return null;
  return function(result) {
    if (result !== m.expect) {
        %ThrowError(JSMSG_PAR_ARRAY_MODE_FAILURE, m.expect, result);
    }
  };
}

function SequentialDo(func, notify) {
  var slices = %ParallelSlices();
  for (var i = 0; i < slices; i++)
    func(i, slices, true);
  for (var i = 0; i < slices; i++)
    func(i, slices, false);
  return true;
}

// Mark the main operations as clone-at-callsite for better precision.
%_SetFunctionFlags(ParallelArrayConstruct0, { cloneAtCallsite: true });
%_SetFunctionFlags(ParallelArrayConstruct1, { cloneAtCallsite: true });
%_SetFunctionFlags(ParallelArrayConstruct2, { cloneAtCallsite: true });
%_SetFunctionFlags(ParallelArrayConstruct3, { cloneAtCallsite: true });
%_SetFunctionFlags(ParallelArrayView,       { cloneAtCallsite: true });
%_SetFunctionFlags(ParallelArrayBuild,      { cloneAtCallsite: true });
%_SetFunctionFlags(ParallelArrayMap,        { cloneAtCallsite: true });
%_SetFunctionFlags(ParallelArrayReduce,     { cloneAtCallsite: true });
%_SetFunctionFlags(ParallelArrayScan,       { cloneAtCallsite: true });
%_SetFunctionFlags(ParallelArrayScatter,    { cloneAtCallsite: true });
%_SetFunctionFlags(ParallelArrayFilter,     { cloneAtCallsite: true });

// Mark the common getters as clone-at-callsite.
%_SetFunctionFlags(ParallelArrayGet1,       { cloneAtCallsite: true });
%_SetFunctionFlags(ParallelArrayGet2,       { cloneAtCallsite: true });
%_SetFunctionFlags(ParallelArrayGet3,       { cloneAtCallsite: true });

// Unit Test Functions
//
// function CheckIndices(shape, index1d) {
//   let idx = ComputeIndices(shape, index1d);
//
//   let c = 0;
//   for (var i = 0; i < shape.length; i++) {
//     var stride = 1;
//     for (var j = i + 1; j < shape.length; j++) {
//       stride *= shape[j];
//     }
//     c += idx[i] * stride;
//   }
//   
//   assertEq(index1d, c);
// }
// 
// for (var q = 0; q < 2*4*6*8; q++) {
//   CheckIndices([2,4,6,8], q);
// }
