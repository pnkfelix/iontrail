/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=78:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef StackExtents_h__
#define StackExtents_h__

namespace js {

struct StackExtents {
    // One link in this chain of linked stack extents.  Each StackExtent
    // is intended to be embedded in some (per-thread allocated)
    // meta-data object.
    struct StackExtent {
        StackExtent *next;

        uintptr_t *stackMin;
        uintptr_t *stackEnd;

#if defined(JS_ION)
        // Either NULL (unset) or, if Ion code is on the stack, and
        // has called into C++, this is aligned to an Ion exit frame.
        uint8_t   *ionTop;

        // Either NULL (unset) or this points to the most recent Ion
        // activation running on the thread.
        js::ion::IonActivation *ionActivation;
#endif

        StackExtent()
            : next(0), stackMin(0), stackEnd(0), ionTop(0), ionActivation(0) {}
    };

    StackExtent *head;
    StackExtents(StackExtent *head) : head(head) {}
};

} // namespace js

#endif // StackExtents_h__
