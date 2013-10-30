if (!this.hasOwnProperty("TypedObject"))
  quit();

var Vec3Type = TypedObject.uint32.array(3);

function foo() {
  for (var i = 0; i < 30000; i += 3) {
    var vec = new Vec3Type([i, i+1, i+2]);
    var sum = vec[0] + vec[1] + vec[2];
    assertEq(sum, 3*i + 3);
  }
}

foo();
