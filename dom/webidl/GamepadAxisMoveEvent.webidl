/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 */

[Pref="dom.gamepad.enabled",
 Constructor(DOMString type, optional GamepadAxisMoveEventInit eventInitDict),
 HeaderFile="GeneratedEventClasses.h"]
interface GamepadAxisMoveEvent : GamepadEvent
{
  readonly attribute unsigned long axis;
  readonly attribute double value;
};

dictionary GamepadAxisMoveEventInit : GamepadEventInit
{
  unsigned long axis = 0;
  double value = 0;
};
