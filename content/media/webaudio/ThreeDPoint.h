/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ThreeDPoint_h_
#define ThreeDPoint_h_

namespace mozilla {

namespace dom {

struct ThreeDPoint {
  ThreeDPoint()
    : x(0.)
    , y(0.)
    , z(0.)
  {
  }
  ThreeDPoint(double aX, double aY, double aZ)
    : x(aX)
    , y(aY)
    , z(aZ)
  {
  }

  double x, y, z;
};

}
}

#endif

