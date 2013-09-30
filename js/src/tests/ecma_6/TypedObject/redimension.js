// |reftest| skip-if(!this.hasOwnProperty("TypedObject"))
var BUGNUMBER = 578700;
var summary = 'Handles';

var T = TypedObject;

function runTests() {
  var counter;

  var Bytes40 = T.uint8.array(40);
  var bytes40 = new Bytes40();
  for (var i = 0, counter = 0; i < 40; i++)
    bytes40[i] = counter++;
  // assertEq(true, bytes40 instanceof Bytes40);

  var Bytes10times4 = T.uint8.array(10, 4);
  var bytes10times4 = bytes40.redimension(Bytes10times4);
  // assertEq(true, bytes10times4 instanceof Bytes10times4);
  counter = 0;
  for (var i = 0; i < 10; i++)
    for (var j = 0; j < 4; j++)
      assertEq(counter++, bytes10times4[i][j]);

  var Bytes2times5times2times2 = T.uint8.array(2, 5, 2, 2);
  var bytes2times5times2times2 = bytes10times4.redimension(Bytes2times5times2times2);
  // assertEq(true, bytes2times5times2times2, Bytes2times5times2times2);
  counter = 0;
  for (var i = 0; i < 2; i++)
    for (var j = 0; j < 5; j++)
      for (var k = 0; k < 2; k++)
        for (var l = 0; l < 2; l++)
          assertEq(counter++, bytes2times5times2times2[i][j][k][l]);

  assertThrows(() => {
    var Bytes10times5 = T.uint8.array(10, 5);
    bytes40.redimension(Bytes10times5);
  });

  assertThrows(() => {
    var Words10times4 = T.uint32.array(10, 5);
    bytes40.redimension(Words10times5);
  });

  reportCompare(true, true);
  print("Tests complete");
}

runTests();


