//
// NBody adapted from Intel's nbody benchmark.
//

load(libdir + "util.js");
load(libdir + "seedrandom.js");

function copyarray(a, leafType) {
  function fill(i) {
    return new Matrix([a[i].length], [leafType], function (j) { return a[i][j]; });
  }
  return fill;
}

var NBody = {
  Constant: {
    "deltaTime": 1,     // 0.005 in their code.
    "epsSqr": 50,       // softening factor, when they compute, set to 50.
    "initialVelocity": 8 // set to 0 to turn off
  },

  init: function init(mode, numBodies, leafType) {
    var initPos = new Array(numBodies);
    var initVel = new Array(numBodies);

    // initialization of inputs
    for (var i = 0; i < numBodies; i++) {
      // [x,y,z]
      initPos[i] = [Math.floor((Math.random()) * 40000),
                    Math.floor((Math.random()) * 20000),
                    Math.floor((Math.random() - .25) * 50000)];

      // [x,y,z,x,y,z]
      initVel[i] = [(Math.random() - 0.5) * NBody.Constant.initialVelocity,
                    (Math.random() - 0.5) * NBody.Constant.initialVelocity,
                    (Math.random()) * NBody.Constant.initialVelocity + 10,

                    (Math.random() - 0.5) * NBody.Constant.initialVelocity,
                    (Math.random() - 0.5) * NBody.Constant.initialVelocity,
                    (Math.random()) * NBody.Constant.initialVelocity];
    }

    NBody.private = {};
    if (mode.slice(0,3) === "par" && typeof(leafType) === "string") {
      NBody.private.pos = new Matrix([numBodies], [3, leafType], copyarray(initPos, leafType));
      NBody.private.vel = new Matrix([numBodies], [6, leafType], copyarray(initVel, leafType));
    } else if (mode === "par") {
      NBody.private.pos = new ParallelArray(initPos);
      NBody.private.vel = new ParallelArray(initVel);
    } else if (mode === "seq") {
      NBody.private.pos = initPos;
      NBody.private.vel = initVel;
    } else {
      throw "unhandled";
    }

    NBody.numBodies = numBodies;
    NBody.time = 0;
  },

  // Parallel Matrix w/ outptr variants

  tickParFloat64Matrix: function tickParFloat64Matrix() {
    var newvel = new Matrix([NBody.numBodies], [6, "float64"], NBody.velocityParOut);
    var newpos = new Matrix([NBody.numBodies], [3, "float64"], NBody.positionParOut);
    NBody.private.vel = newvel; 
    NBody.private.pos = newpos;
    NBody.time++;
  },

  tickParFloat32Matrix: function tickParFloat32Matrix() {
    var newvel = new Matrix([NBody.numBodies], [6, "float32"], NBody.velocityParOut);
    var newpos = new Matrix([NBody.numBodies], [3, "float32"], NBody.positionParOut);
    NBody.private.vel = newvel; 
    NBody.private.pos = newpos;
    NBody.time++;
  },

  tickParAnyMatrix: function tickParAnyMatrix() {
    var newvel = new Matrix([NBody.numBodies], [6, "any"], NBody.velocityParOut);
    var newpos = new Matrix([NBody.numBodies], [3, "any"], NBody.positionParOut);
    NBody.private.vel = newvel; 
    NBody.private.pos = newpos;
    NBody.time++;
  },

  velocityParOut: function velocityParOut(index, outptr) {
    var pos = NBody.private.pos;
    var vel = NBody.private.vel;

    var deltaTime = NBody.Constant.deltaTime;
    var epsSqr = NBody.Constant.epsSqr;
    var time = NBody.time;

    var shape = vel.shape[0];

    var newVel;
    var newX, newY, newZ;
    var newX2, newY2, newZ2;

    var cX = Math.cos(time / 22) * -4200;
    var cY = Math.sin(time / 14) * 9200;
    var cZ = Math.sin(time / 27) * 6000;

    // pull to center
    var maxDistance = 3400;
    var pullStrength = .042;

    var speedLimit = 8;

    // zones
    var zone = 400;
    var repel = 100;
    var align = 300;
    var attract = 100;

    if (time < 500) {
      speedLimit = 2000;
      var attractPower = 100.9;
    } else {
      speedLimit = .2;
      attractPower = 20.9;
    }

    var zoneSqrd = zone * zone + zone * zone + zone * zone;

    var accX = 0, accY = 0, accZ = 0;
    var accX2 = 0, accY2 = 0, accZ2 = 0;
    var i;

    // define particle 1 center distance
    var dirToCenterX = cX - pos.get(index, 0);
    var dirToCenterY = cY - pos.get(index, 1);
    var dirToCenterZ = cZ - pos.get(index, 2);

    var distanceSquaredTo = dirToCenterX * dirToCenterX + dirToCenterY * dirToCenterY + dirToCenterZ * dirToCenterZ;
    var distToCenter = Math.sqrt(distanceSquaredTo);

    // orient to center
    if (distToCenter > maxDistance) {
      var velc = (distToCenter - maxDistance) * pullStrength;
      if (time < 200)
        velc = .2;
      else velc = (distToCenter - maxDistance) * pullStrength;

      accX += (dirToCenterX / distToCenter) * velc;
      accY += (dirToCenterY / distToCenter) * velc;
      accZ += (dirToCenterZ / distToCenter) * velc;
    }

    for (i = 0; i < shape; i = i + 1) {
      var rx = pos.get(i, 0) - pos.get(index, 0);
      var ry = pos.get(i, 1) - pos.get(index, 1);
      var rz = pos.get(i, 2) - pos.get(index, 2);

      // make sure we are not testing the particle against its own position
      var areSame = 0;
      if (pos.get(i, 0) == pos.get(index, 0) && pos.get(i, 1) == pos.get(index, 1) && pos.get(i, 2) == pos.get(index, 2))
        areSame += 1;

      var distSqrd = rx * rx + ry * ry + rz * rz;

      // cant use eqals to test, only <= or >= WTF
      if (distSqrd < zoneSqrd && areSame <= 0) {
        var length = Math.sqrt(distSqrd);
        var percent = distSqrd / zoneSqrd;

        if (distSqrd < repel) {
          var F = (repel / percent - 1) * .025;

          var normalRx = (rx / length) * F;
          var normalRy = (ry / length) * F;
          var normalRz = (rz / length) * F;

          accX = accX + normalRx;
          accY = accY + normalRy;
          accZ = accZ + normalRz;

          accX2 = accX2 - normalRx;
          accY2 = accY2 - normalRy;
          accZ2 = accZ2 - normalRz;
        } else if (distSqrd < align) { //align
          var threshDelta = align - repel;
          var adjustedPercent = (percent - repel) / threshDelta;
          var Q = (.5 - Math.cos(adjustedPercent * 3.14159265 * 2) * .5 + .5) * 100.9;

          // get velocity 2
          var velX2 = vel.get(i, 3);
          var velY2 = vel.get(i, 4);
          var velZ2 = vel.get(i, 5);

          var velLength2 = Math.sqrt(velX2 * velX2 + velY2 * velY2 + velZ2 * velZ2);

          // normalize vel2 and multiply by factor
          velX2 = (velX2 / velLength2) * Q;
          velY2 = (velY2 / velLength2) * Q;
          velZ2 = (velZ2 / velLength2) * Q;

          // get own velocity
          var velX = vel.get(i, 0);
          var velY = vel.get(i, 1);
          var velZ = vel.get(i, 2);

          var velLength = Math.sqrt(velX * velX + velY * velY + velZ * velZ);

          // normalize own velocity
          velX = (velX / velLength) * Q;
          velY = (velY / velLength) * Q;
          velZ = (velZ / velLength) * Q;

          accX += velX2;
          accY += velY2;
          accZ += velZ2;

          accX2 += velX;
          accY2 += velY;
          accZ2 += velZ;
        }

        if (distSqrd > attract) { // attract
          var threshDelta2 = 1 - attract;
          var adjustedPercent2 = (percent - attract) / threshDelta2;
          var C = (1 - (Math.cos(adjustedPercent2 * 3.14159265 * 2) * 0.5 + 0.5)) * attractPower;

          // normalize the distance vector
          var dx = (rx / (length)) * C;
          var dy = (ry / (length)) * C;
          var dz = (rz / (length)) * C;

          accX += dx;
          accY += dy;
          accZ += dz;

          accX2 -= dx;
          accY2 -= dy;
          accZ2 -= dz;
        }
      }
    }

    // enforce speed limits
    if (time > 500) {
      var accSquared = accX * accX + accY * accY + accZ * accZ;
      if (accSquared > speedLimit) {
        accX = accX * .015;
        accY = accY * .015;
        accZ = accZ * .015;
      }

      var accSquared2 = accX2 * accX2 + accY2 * accY2 + accZ2 * accZ2;
      if (accSquared2 > speedLimit) {
        accX2 = accX2 * .015;
        accY2 = accY2 * .015;
        accZ2 = accZ2 * .015;
      }
    }

    // Caclulate new velocity
    newX = (vel.get(index, 0)) + accX;
    newY = (vel.get(index, 1)) + accY;
    newZ = (vel.get(index, 2)) + accZ;

    newX2 = (vel.get(index, 3)) + accX2;
    newY2 = (vel.get(index, 4)) + accY2;
    newZ2 = (vel.get(index, 5)) + accZ2;

    if (time < 500) {
      var acs = newX2 * newX2 + newY2 * newY2 + newZ2 * newZ2;
      if (acs > speedLimit) {
        newX2 = newX2 * .15;
        newY2 = newY2 * .15;
        newZ2 = newZ2 * .15;
      }

      var acs2 = newX * newX + newY * newY + newZ * newZ;
      if (acs2 > speedLimit) {
        newX = newX * .15;
        newY = newY * .15;
        newZ = newZ * .15;
      }
    }

    outptr.set(0, newX);
    outptr.set(1, newY);
    outptr.set(2, newZ);
    outptr.set(3, newX2);
    outptr.set(4, newY2);
    outptr.set(5, newZ2);
    // var ret = [newX, newY, newZ, newX2, newY2, newZ2];
    // return ret;
  },

  positionParOut: function positionParOut(index, outptr) {
    var vel = NBody.private.vel;
    var pos = NBody.private.pos;

    var x = 0;
    var y = 0;
    var z = 0;

    var velX = vel.get(index, 0);
    var velY = vel.get(index, 1);
    var velZ = vel.get(index, 2);

    var velX2 = vel.get(index, 3);
    var velY2 = vel.get(index, 4);
    var velZ2 = vel.get(index, 5);

    var netVelX = (velX - velX2);
    var netVelY = (velY - velY2);
    var netVelZ = (velZ - velZ2);

    x = pos.get(index, 0) + (netVelX);
    y = pos.get(index, 1) + (netVelY);
    z = pos.get(index, 2) + (netVelZ);

    // return [x, y, z];
    outptr.set(0, x);
    outptr.set(1, y);
    outptr.set(2, z);
  },

  // Parallel

  tickPar: function tickPar() {
    var newvel = new ParallelArray([NBody.numBodies], NBody.velocityPar);
    var newpos = new ParallelArray([NBody.numBodies], NBody.positionPar);

    NBody.private.vel = newvel;
    NBody.private.pos = newpos;
    NBody.time++;
  },

  velocityPar: function velocityPar(index) {
    var pos = NBody.private.pos;
    var vel = NBody.private.vel;

    var deltaTime = NBody.Constant.deltaTime;
    var epsSqr = NBody.Constant.epsSqr;
    var time = NBody.time;

    var shape = vel.shape[0];

    var newVel;
    var newX, newY, newZ;
    var newX2, newY2, newZ2;

    var cX = Math.cos(time / 22) * -4200;
    var cY = Math.sin(time / 14) * 9200;
    var cZ = Math.sin(time / 27) * 6000;

    // pull to center
    var maxDistance = 3400;
    var pullStrength = .042;

    var speedLimit = 8;

    // zones
    var zone = 400;
    var repel = 100;
    var align = 300;
    var attract = 100;

    if (time < 500) {
      speedLimit = 2000;
      var attractPower = 100.9;
    } else {
      speedLimit = .2;
      attractPower = 20.9;
    }

    var zoneSqrd = zone * zone + zone * zone + zone * zone;

    var accX = 0, accY = 0, accZ = 0;
    var accX2 = 0, accY2 = 0, accZ2 = 0;
    var i;

    // define particle 1 center distance
    var dirToCenterX = cX - pos.get(index)[0];
    var dirToCenterY = cY - pos.get(index)[1];
    var dirToCenterZ = cZ - pos.get(index)[2];

    var distanceSquaredTo = dirToCenterX * dirToCenterX + dirToCenterY * dirToCenterY + dirToCenterZ * dirToCenterZ;
    var distToCenter = Math.sqrt(distanceSquaredTo);

    // orient to center
    if (distToCenter > maxDistance) {
      var velc = (distToCenter - maxDistance) * pullStrength;
      if (time < 200)
        velc = .2;
      else velc = (distToCenter - maxDistance) * pullStrength;

      accX += (dirToCenterX / distToCenter) * velc;
      accY += (dirToCenterY / distToCenter) * velc;
      accZ += (dirToCenterZ / distToCenter) * velc;
    }

    for (i = 0; i < shape; i = i + 1) {
      var rx = pos.get(i)[0] - pos.get(index)[0];
      var ry = pos.get(i)[1] - pos.get(index)[1];
      var rz = pos.get(i)[2] - pos.get(index)[2];

      // make sure we are not testing the particle against its own position
      var areSame = 0;
      if (pos.get(i)[0] == pos.get(index)[0] && pos.get(i)[1] == pos.get(index)[1] && pos.get(i)[2] == pos.get(index)[2])
        areSame += 1;

      var distSqrd = rx * rx + ry * ry + rz * rz;

      // cant use eqals to test, only <= or >= WTF
      if (distSqrd < zoneSqrd && areSame <= 0) {
        var length = Math.sqrt(distSqrd);
        var percent = distSqrd / zoneSqrd;

        if (distSqrd < repel) {
          var F = (repel / percent - 1) * .025;

          var normalRx = (rx / length) * F;
          var normalRy = (ry / length) * F;
          var normalRz = (rz / length) * F;

          accX = accX + normalRx;
          accY = accY + normalRy;
          accZ = accZ + normalRz;

          accX2 = accX2 - normalRx;
          accY2 = accY2 - normalRy;
          accZ2 = accZ2 - normalRz;
        } else if (distSqrd < align) { //align
          var threshDelta = align - repel;
          var adjustedPercent = (percent - repel) / threshDelta;
          var Q = (.5 - Math.cos(adjustedPercent * 3.14159265 * 2) * .5 + .5) * 100.9;

          // get velocity 2
          var velX2 = vel.get(i)[3];
          var velY2 = vel.get(i)[4];
          var velZ2 = vel.get(i)[5];

          var velLength2 = Math.sqrt(velX2 * velX2 + velY2 * velY2 + velZ2 * velZ2);

          // normalize vel2 and multiply by factor
          velX2 = (velX2 / velLength2) * Q;
          velY2 = (velY2 / velLength2) * Q;
          velZ2 = (velZ2 / velLength2) * Q;

          // get own velocity
          var velX = vel.get(i)[0];
          var velY = vel.get(i)[1];
          var velZ = vel.get(i)[2];

          var velLength = Math.sqrt(velX * velX + velY * velY + velZ * velZ);

          // normalize own velocity
          velX = (velX / velLength) * Q;
          velY = (velY / velLength) * Q;
          velZ = (velZ / velLength) * Q;

          accX += velX2;
          accY += velY2;
          accZ += velZ2;

          accX2 += velX;
          accY2 += velY;
          accZ2 += velZ;
        }

        if (distSqrd > attract) { // attract
          var threshDelta2 = 1 - attract;
          var adjustedPercent2 = (percent - attract) / threshDelta2;
          var C = (1 - (Math.cos(adjustedPercent2 * 3.14159265 * 2) * 0.5 + 0.5)) * attractPower;

          // normalize the distance vector
          var dx = (rx / (length)) * C;
          var dy = (ry / (length)) * C;
          var dz = (rz / (length)) * C;

          accX += dx;
          accY += dy;
          accZ += dz;

          accX2 -= dx;
          accY2 -= dy;
          accZ2 -= dz;
        }
      }
    }

    // enforce speed limits
    if (time > 500) {
      var accSquared = accX * accX + accY * accY + accZ * accZ;
      if (accSquared > speedLimit) {
        accX = accX * .015;
        accY = accY * .015;
        accZ = accZ * .015;
      }

      var accSquared2 = accX2 * accX2 + accY2 * accY2 + accZ2 * accZ2;
      if (accSquared2 > speedLimit) {
        accX2 = accX2 * .015;
        accY2 = accY2 * .015;
        accZ2 = accZ2 * .015;
      }
    }

    // Caclulate new velocity
    newX = (vel.get(index)[0]) + accX;
    newY = (vel.get(index)[1]) + accY;
    newZ = (vel.get(index)[2]) + accZ;

    newX2 = (vel.get(index)[3]) + accX2;
    newY2 = (vel.get(index)[4]) + accY2;
    newZ2 = (vel.get(index)[5]) + accZ2;

    if (time < 500) {
      var acs = newX2 * newX2 + newY2 * newY2 + newZ2 * newZ2;
      if (acs > speedLimit) {
        newX2 = newX2 * .15;
        newY2 = newY2 * .15;
        newZ2 = newZ2 * .15;
      }

      var acs2 = newX * newX + newY * newY + newZ * newZ;
      if (acs2 > speedLimit) {
        newX = newX * .15;
        newY = newY * .15;
        newZ = newZ * .15;
      }
    }

    return [newX, newY, newZ, newX2, newY2, newZ2];
  },

  positionPar: function positionPar(index) {
    var vel = NBody.private.vel;
    var pos = NBody.private.pos;

    var x = 0;
    var y = 0;
    var z = 0;

    var velX = vel.get(index)[0];
    var velY = vel.get(index)[1];
    var velZ = vel.get(index)[2];

    var velX2 = vel.get(index)[3];
    var velY2 = vel.get(index)[4];
    var velZ2 = vel.get(index)[5];

    var netVelX = (velX - velX2);
    var netVelY = (velY - velY2);
    var netVelZ = (velZ - velZ2);

    x = pos.get(index)[0] + (netVelX);
    y = pos.get(index)[1] + (netVelY);
    z = pos.get(index)[2] + (netVelZ);

    return [x, y, z];
  },

  // Sequential

  tickSeq: function tickSeq() {
    var numBodies = NBody.numBodies;
    var newVel = new Array(numBodies);
    var newPos = new Array(numBodies);

    for (var i = 0; i < numBodies; i++)
      newVel[i] = NBody.velocitySeq(i);

    for (var i = 0; i < numBodies; i++)
      newPos[i] = NBody.positionSeq(i);

    NBody.private.vel = newVel;
    NBody.private.pos = newPos;

    NBody.time++;
  },

  velocitySeq: function velocitySeq(index) {
    var pos = NBody.private.pos;
    var vel = NBody.private.vel;

    var deltaTime = NBody.Constant.deltaTime;
    var epsSqr = NBody.Constant.epsSqr;
    var time = NBody.time;

    var shape = pos.length;

    var newVel;
    var newX, newY, newZ;
    var newX2, newY2, newZ2;

    var cX = Math.cos(time / 22) * -4200;
    var cY = Math.sin(time / 14) * 9200;
    var cZ = Math.sin(time / 27) * 6000;

    // pull to center
    var maxDistance = 3400;
    var pullStrength = .042;

    var speedLimit = 8;

    // zones
    var zone = 400;
    var repel = 100;
    var align = 300;
    var attract = 100;

    if (time < 500) {
      speedLimit = 2000;
      var attractPower = 100.9;
    } else {
      speedLimit = .2;
      attractPower = 20.9;
    }

    var zoneSqrd = zone * zone + zone * zone + zone * zone;

    var accX = 0, accY = 0, accZ = 0;
    var accX2 = 0, accY2 = 0, accZ2 = 0;
    var i;

    // define particle 1 center distance
    var dirToCenterX = cX - pos[index][0];
    var dirToCenterY = cY - pos[index][1];
    var dirToCenterZ = cZ - pos[index][2];

    var distanceSquaredTo = dirToCenterX * dirToCenterX + dirToCenterY * dirToCenterY + dirToCenterZ * dirToCenterZ;
    var distToCenter = Math.sqrt(distanceSquaredTo);

    // orient to center
    if (distToCenter > maxDistance) {
      var velc = (distToCenter - maxDistance) * pullStrength;
      if (time < 200)
        velc = .2;
      else velc = (distToCenter - maxDistance) * pullStrength;

      accX += (dirToCenterX / distToCenter) * velc;
      accY += (dirToCenterY / distToCenter) * velc;
      accZ += (dirToCenterZ / distToCenter) * velc;
    }

    for (i = 0; i < shape; i = i + 1) {
      var rx = pos[i][0] - pos[index][0];
      var ry = pos[i][1] - pos[index][1];
      var rz = pos[i][2] - pos[index][2];

      // make sure we are not testing the particle against its own position
      var areSame = 0;
      if (pos[i][0] == pos[index][0] && pos[i][1] == pos[index][1] && pos[i][2] == pos[index][2])
        areSame += 1;

      var distSqrd = rx * rx + ry * ry + rz * rz;

      // cant use eqals to test, only <= or >= WTF
      if (distSqrd < zoneSqrd && areSame <= 0) {
        var length = Math.sqrt(distSqrd);
        var percent = distSqrd / zoneSqrd;

        if (distSqrd < repel) {
          var F = (repel / percent - 1) * .025;

          var normalRx = (rx / length) * F;
          var normalRy = (ry / length) * F;
          var normalRz = (rz / length) * F;

          accX = accX + normalRx;
          accY = accY + normalRy;
          accZ = accZ + normalRz;

          accX2 = accX2 - normalRx;
          accY2 = accY2 - normalRy;
          accZ2 = accZ2 - normalRz;
        } else if (distSqrd < align) { //align
          var threshDelta = align - repel;
          var adjustedPercent = (percent - repel) / threshDelta;
          var Q = (.5 - Math.cos(adjustedPercent * 3.14159265 * 2) * .5 + .5) * 100.9;

          // get velocity 2
          var velX2 = vel[i][3];
          var velY2 = vel[i][4];
          var velZ2 = vel[i][5];

          var velLength2 = Math.sqrt(velX2 * velX2 + velY2 * velY2 + velZ2 * velZ2);

          // normalize vel2 and multiply by factor
          velX2 = (velX2 / velLength2) * Q;
          velY2 = (velY2 / velLength2) * Q;
          velZ2 = (velZ2 / velLength2) * Q;

          // get own velocity
          var velX = vel[i][0];
          var velY = vel[i][1];
          var velZ = vel[i][2];

          var velLength = Math.sqrt(velX * velX + velY * velY + velZ * velZ);

          // normalize own velocity
          velX = (velX / velLength) * Q;
          velY = (velY / velLength) * Q;
          velZ = (velZ / velLength) * Q;

          accX += velX2;
          accY += velY2;
          accZ += velZ2;

          accX2 += velX;
          accY2 += velY;
          accZ2 += velZ;
        }

        if (distSqrd > attract) { // attract
          var threshDelta2 = 1 - attract;
          var adjustedPercent2 = (percent - attract) / threshDelta2;
          var C = (1 - (Math.cos(adjustedPercent2 * 3.14159265 * 2) * 0.5 + 0.5)) * attractPower;

          // normalize the distance vector
          var dx = (rx / (length)) * C;
          var dy = (ry / (length)) * C;
          var dz = (rz / (length)) * C;

          accX += dx;
          accY += dy;
          accZ += dz;

          accX2 -= dx;
          accY2 -= dy;
          accZ2 -= dz;
        }
      }
    }

    // enforce speed limits
    if (time > 500) {
      var accSquared = accX * accX + accY * accY + accZ * accZ;
      if (accSquared > speedLimit) {
        accX = accX * .015;
        accY = accY * .015;
        accZ = accZ * .015;
      }

      var accSquared2 = accX2 * accX2 + accY2 * accY2 + accZ2 * accZ2;
      if (accSquared2 > speedLimit) {
        accX2 = accX2 * .015;
        accY2 = accY2 * .015;
        accZ2 = accZ2 * .015;
      }
    }

    // Caclulate new velocity
    newX = vel[index][0] + accX;
    newY = vel[index][1] + accY;
    newZ = vel[index][2] + accZ;

    newX2 = vel[index][3] + accX2;
    newY2 = vel[index][4] + accY2;
    newZ2 = vel[index][5] + accZ2;

    if (time < 500) {
      var acs = newX2 * newX2 + newY2 * newY2 + newZ2 * newZ2;
      if (acs > speedLimit) {
        newX2 = newX2 * .15;
        newY2 = newY2 * .15;
        newZ2 = newZ2 * .15;
      }

      var acs2 = newX * newX + newY * newY + newZ * newZ;
      if (acs2 > speedLimit) {
        newX = newX * .15;
        newY = newY * .15;
        newZ = newZ * .15;
      }
    }

    return [newX, newY, newZ, newX2, newY2, newZ2];
  },

  positionSeq: function positionSeq(index) {
    var vel = NBody.private.vel;
    var pos = NBody.private.pos;

    var x = 0;
    var y = 0;
    var z = 0;

    var velX = vel[index][0];
    var velY = vel[index][1];
    var velZ = vel[index][2];

    var velX2 = vel[index][3];
    var velY2 = vel[index][4];
    var velZ2 = vel[index][5];

    var netX = (velX - velX2);
    var netY = (velY - velY2);
    var netZ = (velZ - velZ2);

    x = pos[index][0] + netX;
    y = pos[index][1] + netY;
    z = pos[index][2] + netZ;

    return [x, y, z];
  }
};

function matrixToArrays(m) {
  var a = [];
  if (m.shape.length == 1) {
    for (var i = 0; i < m.shape[0]; i++) {
      a.push(m.get(i));
    }
  } else {
    for (var i = 0; i < m.shape[0]; i++) {
      a.push(matrixToArrays(m.get(i)));
    }
  }
  return a;
}

function emulateNBody(mode, numBodies, ticks, leafType) {
  NBody.init(mode, numBodies, leafType);
  for (var i = 0; i < ticks; i++) {
    var start = Date.now();
    if (mode === "par")
      NBody.tickPar();
    else if (mode === "par-float64-matrix")
      NBody.tickParFloat64Matrix();
    else if (mode === "par-float32-matrix")
      NBody.tickParFloat32Matrix();
    else if (mode === "par-any-matrix")
      NBody.tickParAnyMatrix();
    else
      NBody.tickSeq();
    //print(NBody.private.pos);
    print(mode + " bodies=" + numBodies + " tick=" + (i+1) + "/" + ticks + ": " + (Date.now() - start) + " ms");
  }
  var ret;
  if (mode.slice(0,3) === "par") {
    ret = {pos:matrixToArrays(NBody.private.pos),
           vel:matrixToArrays(NBody.private.vel)};
  } else {
    ret = NBody.private;
  }
  return ret;
}

// Using 4000 bodies going off Rick's comment as 4000 being a typical workload.
const NUMBODIES = 4000;
const TICKS = 10;

function reseed() { Math.seedrandom("seed"); }

function seq_vs_par() {
  function seq() { reseed(); return emulateNBody("seq", NUMBODIES, TICKS); }
  function par() { reseed(); return emulateNBody("par", NUMBODIES, TICKS); }

  var seq_params = new BenchParams(1, "sequential", "SEQ", 1, 1, seq);
  var par_params = new BenchParams(2, "parallel",   "PAR", 1, 1, par);
  benchmark_generic(3, "NBODY", seq_params, par_params);
}

function seq_vs_matrix() {
  function seq() { reseed(); return emulateNBody("seq",            NUMBODIES, TICKS); }
  function par() { reseed(); return emulateNBody("par-any-matrix", NUMBODIES, TICKS, "any"); }

  var seq_params = new BenchParams(1, "sequential", "SEQ", 1, 1, seq);
  var par_params = new BenchParams(2, "par-matrix", "MAT", 1, 1, par);
  benchmark_generic(3, "NBODY", seq_params, par_params);
}

function array_vs_matrix() {
  function f() { reseed(); return emulateNBody("par",            NUMBODIES, TICKS); }
  function g() { reseed(); return emulateNBody("par-any-matrix", NUMBODIES, TICKS, "any"); }

  var params1 = new BenchParams(1, "array",  "ARR", 1, 1, f);
  var params2 = new BenchParams(2, "matrix", "MAT", 1, 1, g);
  benchmark_generic(3, "NBODY", params1, params2);
}

function any_vs_float64() {
  function f() { reseed(); return emulateNBody("par-any-matrix",     NUMBODIES, TICKS, "any"); }
  function g() { reseed(); return emulateNBody("par-float64-matrix", NUMBODIES, TICKS, "float64"); }

  var params1 = new BenchParams(1, "matrix-any",     "ANY", 1, 1, f);
  var params2 = new BenchParams(2, "matrix-float64", "F64", 1, 1, g);
  benchmark_generic(3, "NBODY", params1, params2);
}

function float32_vs_float64() {
  function f() { reseed(); return emulateNBody("par-float32-matrix", NUMBODIES, TICKS, "float32"); }
  function g() { reseed(); return emulateNBody("par-float64-matrix", NUMBODIES, TICKS, "float64"); }

  var params1 = new BenchParams(1, "matrix-float32", "F32", 1, 1, f);
  var params2 = new BenchParams(2, "matrix-float64", "F64", 1, 1, g);
  benchmark_generic(3, "NBODY", params1, params2);
}

try {
  // seq_vs_par();
  seq_vs_matrix();
  // array_vs_matrix();
  // any_vs_float64();     // Does not work yet
  // float32_vs_float64(); // Does not work yet
} catch (e) {
  print(e.name);
  print(e.message);
  print(e.stack);
  throw e;
}
