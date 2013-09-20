#include "TypedObjectConstants.h"

///////////////////////////////////////////////////////////////////////////
// Getters and setters for various slots.

// Type object slots

#define TYPE_TYPE_REPR(obj) \
    UnsafeGetReservedSlot(obj, JS_TYPEOBJ_SLOT_TYPE_REPR)

// Typed object slots

#define TYPED_TYPE_OBJ(obj) \
    UnsafeGetReservedSlot(obj, JS_TYPEDOBJ_SLOT_TYPE_OBJ)
#define TYPED_OWNER(obj) \
    UnsafeGetReservedSlot(obj, JS_TYPEDOBJ_SLOT_OWNER)

// Type repr slots

#define REPR_KIND(obj)   \
    TO_INT32(UnsafeGetReservedSlot(obj, JS_TYPEREPR_SLOT_KIND))
#define REPR_SIZE(obj)   \
    TO_INT32(UnsafeGetReservedSlot(obj, JS_TYPEREPR_SLOT_SIZE))
#define REPR_ALIGNMENT(obj) \
    TO_INT32(UnsafeGetReservedSlot(obj, JS_TYPEREPR_SLOT_ALIGNMENT))
#define REPR_LENGTH(obj)   \
    TO_INT32(UnsafeGetReservedSlot(obj, JS_TYPEREPR_SLOT_LENGTH))
#define REPR_TYPE(obj)   \
    TO_INT32(UnsafeGetReservedSlot(obj, JS_TYPEREPR_SLOT_TYPE))

#define HAS_PROPERTY(obj, prop) \
    callFunction(std_Object_hasOwnProperty, obj, prop)

function TYPED_TYPE_REPR(obj) {
  // Eventually this will be a slot on typed objects
  return TYPE_TYPE_REPR(TYPED_TYPE_OBJ(obj));
}

///////////////////////////////////////////////////////////////////////////
// TypedObjectPointer
//
// TypedObjectPointers are internal structs used to represent a
// pointer into typed object memory. They pull together:
// - typeRepr: the internal type representation
// - typeObj: the user-visible type object
// - typedDatum: the typed object that contains the allocated block of memory
// - offset: an offset into that typed object
//
// They are basically equivalent to a typed object, except that they
// offer lots of internal unsafe methods and are not native objects.
// These should never escape into user code; ideally ion would stack
// allocate them.
//
// Most `TypedObjectPointers` methods are written in a "chaining"
// style, meaning that they return `this`. This is true even though
// they mutate the receiver in place, because it makes for prettier
// code.

function TypedObjectPointer(typeRepr, typeObj, typedDatum, offset) {
  this.typeRepr = typeRepr;
  this.typeObj = typeObj;
  this.typedDatum = typedDatum;
  this.offset = offset;
}

MakeConstructible(TypedObjectPointer, {});

TypedObjectPointer.fromTypedDatum = function(typed) {
  return new TypedObjectPointer(TYPED_TYPE_REPR(typed),
                                TYPED_TYPE_OBJ(typed),
                                typed,
                                0);
}

#ifdef DEBUG
TypedObjectPointer.prototype.toString = function() {
  return "Ptr(" + this.typeObj.toSource() + " @ " + this.offset + ")";
};
#endif

TypedObjectPointer.prototype.copy = function() {
  return new TypedObjectPointer(this.typeRepr, this.typeObj,
                                this.typedDatum, this.offset);
};

TypedObjectPointer.prototype.reset = function(inPtr) {
  this.typeRepr = inPtr.typeRepr;
  this.typeObj = inPtr.typeObj;
  this.typedDatum = inPtr.typedDatum;
  this.offset = inPtr.offset;
  return this;
};

TypedObjectPointer.prototype.kind = function() {
  return REPR_KIND(this.typeRepr);
}

///////////////////////////////////////////////////////////////////////////
// Moving the pointer
//
// The methods in this section adjust `this` in place to point at
// subelements or subproperties.

// Adjusts `this` in place so that it points at the property
// `propName`.  Throws if there is no such property. Returns `this`.
TypedObjectPointer.prototype.moveTo = function(propName) {
  switch (this.kind()) {
  case JS_TYPEREPR_SCALAR_KIND:
    break;

  case JS_TYPEREPR_ARRAY_KIND:
    // For an array, property must be an element. Note that we use the
    // length as loaded from the type *representation* as opposed to
    // the type *object*; this is because some type objects represent
    // unsized arrays and hence do not have a length.
    var index = TO_INT32(propName);
    if (index === propName && index < REPR_LENGTH(this.typeRepr))
      return this.moveToElem(index);
    break;

  case JS_TYPEREPR_STRUCT_KIND:
    if (HAS_PROPERTY(this.typeObj.fieldTypes, propName))
      return this.moveToField(propName);
    break;
  }

  ThrowError(JSMSG_TYPEDOBJECT_NO_SUCH_PROP, propName);
};

// Adjust `this` in place to point at the element `index`.  `this`
// must be a array type and `index` must be within bounds. Returns
// `this`.
TypedObjectPointer.prototype.moveToElem = function(index) {
  assert(this.kind() == JS_TYPEREPR_ARRAY_KIND,
         "moveToElem invoked on non-array");
  assert(index < REPR_LENGTH(this.typeRepr),
         "moveToElem invoked with out-of-bounds index");

  var elementTypeObj = this.typeObj.elementType;
  var elementTypeRepr = TYPE_TYPE_REPR(elementTypeObj);
  this.typeRepr = elementTypeRepr;
  this.typeObj = elementTypeObj;
  var elementSize = REPR_SIZE(elementTypeRepr);

  // Note: we do not allow construction of arrays where the offset
  // of an element cannot be represented by an int32.
  this.offset += std_Math_imul(index, elementSize);

  return this;
};

// Adjust `this` to point at the field `propName`.  `this` must be a
// struct type and `propName` must be a valid field name. Returns
// `this`.
TypedObjectPointer.prototype.moveToField = function(propName) {
  assert(this.kind() == JS_TYPEREPR_STRUCT_KIND,
         "moveToField invoked on non-struct");
  assert(HAS_PROPERTY(this.typeObj.fieldTypes, propName),
         "moveToField invoked with undefined field");

  var fieldTypeObj = this.typeObj.fieldTypes[propName];
  var fieldOffset = TO_INT32(this.typeObj.fieldOffsets[propName]);
  this.typeObj = fieldTypeObj;
  this.typeRepr = TYPE_TYPE_REPR(fieldTypeObj);

  // Note: we do not allow construction of structs where the
  // offset of a field cannot be represented by an int32.
  this.offset += fieldOffset;

  return this;
}

///////////////////////////////////////////////////////////////////////////
// Getting values
//
// The methods in this section read from the memory pointed at
// by `this` and produce JS values. This process is called *reification*
// spec.


// Reifies the value referenced by the pointer, meaning that it
// returns a new object pointing at the value. If the value is
// a scalar, it will return a JS number, but otherwise the reified
// result will be a typed object or handle, depending on the type
// of the ptr's typedDatum.
TypedObjectPointer.prototype.get = function() {
  assert(ObjectIsAttached(this.typedDatum), "get() called with unattached typedDatum");

  if (REPR_KIND(this.typeRepr) == JS_TYPEREPR_SCALAR_KIND)
    return this.getScalar();

  return NewDerivedTypedDatum(this.typeObj, this.typedDatum, this.offset);
}

TypedObjectPointer.prototype.getScalar = function() {
  var type = REPR_TYPE(this.typeRepr);
  switch (type) {
  case JS_SCALARTYPEREPR_INT8:
    return Load_int8(this.typedDatum, this.offset);

  case JS_SCALARTYPEREPR_UINT8:
  case JS_SCALARTYPEREPR_UINT8_CLAMPED:
    return Load_uint8(this.typedDatum, this.offset);

  case JS_SCALARTYPEREPR_INT16:
    return Load_int16(this.typedDatum, this.offset);

  case JS_SCALARTYPEREPR_UINT16:
    return Load_uint16(this.typedDatum, this.offset);

  case JS_SCALARTYPEREPR_INT32:
    return Load_int32(this.typedDatum, this.offset);

  case JS_SCALARTYPEREPR_UINT32:
    return Load_uint32(this.typedDatum, this.offset);

  case JS_SCALARTYPEREPR_FLOAT32:
    return Load_float32(this.typedDatum, this.offset);

  case JS_SCALARTYPEREPR_FLOAT64:
    return Load_float64(this.typedDatum, this.offset);
  }

  assert(false, "Unhandled scalar type: " + type);
}

///////////////////////////////////////////////////////////////////////////
// Setting values
//
// The methods in this section modify the data pointed at by `this`.

// Assigns `fromValue` to the memory pointed at by `this`, adapting it
// to `typeRepr` as needed. This is the most general entry point and
// works for any type.
TypedObjectPointer.prototype.set = function(fromValue) {
  assert(ObjectIsAttached(this.typedDatum), "set() called with unattached typedDatum");

  var typeRepr = this.typeRepr;

  // Fast path: `fromValue` is a typed object with same type
  // representation as the destination. In that case, we can just do a
  // memcpy.
  if (IsObject(fromValue) && HaveSameClass(fromValue, this.typedDatum)) {
    if (TYPED_TYPE_REPR(fromValue) === typeRepr) {
      if (!ObjectIsAttached(fromValue))
        ThrowError(JSMSG_TYPEDOBJECT_HANDLE_UNATTACHED);

      var size = REPR_SIZE(typeRepr);
      Memcpy(this.typedDatum, this.offset, fromValue, 0, size);
      return;
    }
  }

  switch (REPR_KIND(typeRepr)) {
  case JS_TYPEREPR_SCALAR_KIND:
    this.setScalar(fromValue);
    return;

  case JS_TYPEREPR_ARRAY_KIND:
    if (!IsObject(fromValue))
      break;

    // Check that "array-like" fromValue has an appropriate length.
    var length = REPR_LENGTH(typeRepr);
    if (fromValue.length !== length)
      break;

    // Adapt each element.
    var tempPtr = this.copy().moveToElem(0);
    var size = REPR_SIZE(tempPtr.typeRepr);
    for (var i = 0; i < length; i++) {
      tempPtr.set(fromValue[i]);
      tempPtr.offset += size;
    }
    return;

  case JS_TYPEREPR_STRUCT_KIND:
    if (!IsObject(fromValue))
      break;

    // Adapt each field.
    var tempPtr = this.copy();
    var fieldNames = this.typeObj.fieldNames;
    for (var i = 0; i < fieldNames.length; i++) {
      var fieldName = fieldNames[i];
      tempPtr.reset(this).moveToField(fieldName).set(fromValue[fieldName]);
    }
    return;
  }

  ThrowError(JSMSG_CANT_CONVERT_TO,
             typeof(fromValue),
             this.typeRepr.toSource())
}

// Sets `fromValue` to `this` assuming that `this` is a scalar type.
TypedObjectPointer.prototype.setScalar = function(fromValue) {
  assert(REPR_KIND(this.typeRepr) == JS_TYPEREPR_SCALAR_KIND,
         "setScalar called with non-scalar");

  var type = REPR_TYPE(this.typeRepr);
  switch (type) {
  case JS_SCALARTYPEREPR_INT8:
    return Store_int8(this.typedDatum, this.offset,
                     TO_INT32(fromValue) & 0xFF);

  case JS_SCALARTYPEREPR_UINT8:
    return Store_uint8(this.typedDatum, this.offset,
                      TO_UINT32(fromValue) & 0xFF);

  case JS_SCALARTYPEREPR_UINT8_CLAMPED:
    var v = ClampToUint8(+fromValue);
    return Store_int8(this.typedDatum, this.offset, v);

  case JS_SCALARTYPEREPR_INT16:
    return Store_int16(this.typedDatum, this.offset,
                      TO_INT32(fromValue) & 0xFFFF);

  case JS_SCALARTYPEREPR_UINT16:
    return Store_uint16(this.typedDatum, this.offset,
                       TO_UINT32(fromValue) & 0xFFFF);

  case JS_SCALARTYPEREPR_INT32:
    return Store_int32(this.typedDatum, this.offset,
                      TO_INT32(fromValue));

  case JS_SCALARTYPEREPR_UINT32:
    return Store_uint32(this.typedDatum, this.offset,
                       TO_UINT32(fromValue));

  case JS_SCALARTYPEREPR_FLOAT32:
    return Store_float32(this.typedDatum, this.offset, +fromValue);

  case JS_SCALARTYPEREPR_FLOAT64:
    return Store_float64(this.typedDatum, this.offset, +fromValue);
  }

  assert(false, "Unhandled scalar type: " + type);
}

///////////////////////////////////////////////////////////////////////////
// C++ Wrappers
//
// These helpers are invoked by C++ code or used as method bodies.

// Wrapper for use from C++ code.
function ConvertAndCopyTo(destTypeRepr,
                          destTypeObj,
                          destTypedObj,
                          destOffset,
                          fromValue)
{
  assert(IsObject(destTypeRepr) && ObjectIsTypeRepresentation(destTypeRepr),
         "ConvertAndCopyTo: not type repr");
  assert(IsObject(destTypeObj) && ObjectIsTypeObject(destTypeObj),
         "ConvertAndCopyTo: not type obj");

  if (!ObjectIsAttached(destTypedObj))
    ThrowError(JSMSG_TYPEDOBJECT_HANDLE_UNATTACHED);

  var ptr = new TypedObjectPointer(destTypeRepr, destTypeObj,
                                   destTypedObj, destOffset);
  ptr.set(fromValue);
}

// Wrapper for use from C++ code.
function Reify(sourceTypeRepr,
               sourceTypeObj,
               sourceTypedObj,
               sourceOffset) {
  assert(IsObject(sourceTypeRepr) && ObjectIsTypeRepresentation(sourceTypeRepr),
         "Reify: not type repr");
  assert(IsObject(sourceTypeObj) && ObjectIsTypeObject(sourceTypeObj),
         "Reify: not type obj");

  if (!ObjectIsAttached(sourceTypedObj))
    ThrowError(JSMSG_TYPEDOBJECT_HANDLE_UNATTACHED);

  var ptr = new TypedObjectPointer(sourceTypeRepr, sourceTypeObj,
                                   sourceTypedObj, sourceOffset);

  return ptr.get();
}

function FillTypedArrayWithValue(destArray, fromValue) {
  var typeRepr = TYPED_TYPE_REPR(destArray);
  var length = REPR_LENGTH(typeRepr);
  if (length === 0)
    return;

  // Use convert and copy to to produce the first element:
  var ptr = TypedObjectPointer.fromTypedDatum(destArray);
  ptr.moveToElem(0);
  ptr.set(fromValue);

  // Stamp out the remaining copies:
  var elementSize = REPR_SIZE(ptr.typeRepr);
  var totalSize = length * elementSize;
  for (var offset = elementSize; offset < totalSize; offset += elementSize)
    Memcpy(destArray, offset, destArray, 0, elementSize);
}

// Warning: user exposed!
function TypeObjectEquivalent(otherTypeObj) {
  if (!IsObject(this) || !ObjectIsTypeObject(this))
    ThrowError(JSMSG_TYPEDOBJECT_HANDLE_BAD_ARGS, "this", "type object");
  if (!IsObject(otherTypeObj) || !ObjectIsTypeObject(otherTypeObj))
    ThrowError(JSMSG_TYPEDOBJECT_HANDLE_BAD_ARGS, "1", "type object");
  return TYPE_TYPE_REPR(this) === TYPE_TYPE_REPR(otherTypeObj);
}

// Warning: user exposed!
function TypedArrayRedimension(newArrayType) {
  if (!IsObject(this) || !ObjectIsTypedDatum(this))
    ThrowError(JSMSG_TYPEDOBJECT_HANDLE_BAD_ARGS, "this", "typed array");

  var oldArrayType = TYPED_TYPE_OBJ(this);
  var oldElementType = oldArrayType;
  var oldElementCount = 1;
  while (REPR_KIND(TYPE_TYPE_REPR(oldElementType)) == JS_TYPEREPR_ARRAY_KIND) {
    oldElementCount *= oldElementType.length;
    oldElementType = oldElementType.elementType;
  }

  if (!IsObject(newArrayType) || !ObjectIsTypeObject(newArrayType))
    ThrowError(JSMSG_TYPEDOBJECT_HANDLE_BAD_ARGS, 1, "type object");

  var newElementType = newArrayType;
  var newElementCount = 1;
  while (REPR_KIND(TYPE_TYPE_REPR(newElementType)) == JS_TYPEREPR_ARRAY_KIND) {
    newElementCount *= newElementType.length;
    newElementType = newElementType.elementType;
  }

  if (oldElementCount !== newElementCount)
    ThrowError(JSMSG_TYPEDOBJECT_HANDLE_BAD_ARGS,
               1,
               "New number of elements does not match old number of elements");

  if (!oldElementType.equivalent(newElementType))
    ThrowError(JSMSG_TYPEDOBJECT_HANDLE_BAD_ARGS,
               1,
               "New element type " + newElementType.toSource() +
               "does not match old element type " + oldElementType.toSource());

  assert(REPR_SIZE(TYPE_TYPE_REPR(oldArrayType)) ==
         REPR_SIZE(TYPE_TYPE_REPR(newArrayType)),
         "Byte sizes should be equal");

  return NewDerivedTypedDatum(newArrayType, this, 0);
}

///////////////////////////////////////////////////////////////////////////
// Handles
//
// Note: these methods are directly invokable by users and so must be
// defensive.

// This is the `handle([obj, [...path]])` method on type objects.
function HandleCreate(obj, ...path) {
  if (!ObjectIsTypeObject(this))
    ThrowError(JSMSG_INCOMPATIBLE_PROTO, "Type", "handle", "value");

  var handle = NewTypedHandle(this);

  if (obj !== undefined)
    HandleMoveInternal(handle, obj, path)

  return handle;
}

// Handle.move: user exposed!
function HandleMove(handle, obj, ...path) {
  if (!ObjectIsTypedHandle(handle))
    ThrowError(JSMSG_INCOMPATIBLE_PROTO, "Handle", "set", typeof value);

  HandleMoveInternal(handle, obj, path);
}

function HandleMoveInternal(handle, obj, path) {
  assert(ObjectIsTypedHandle(handle),
         "HandleMoveInternal: not typed handle");

  if (!IsObject(obj) || !ObjectIsTypedDatum(obj))
    ThrowError(JSMSG_INCOMPATIBLE_PROTO);

  var ptr = TypedObjectPointer.fromTypedDatum(obj);
  for (var i = 0; i < path.length; i++)
    ptr.moveTo(path[i]);

  AttachHandle(handle, ptr.typedDatum, ptr.offset)
}

// Handle.get: user exposed!
function HandleGet(handle) {
  if (!ObjectIsTypedHandle(handle))
    ThrowError(JSMSG_INCOMPATIBLE_PROTO, "Handle", "set", typeof value);

  if (!ObjectIsAttached(handle))
    ThrowError(JSMSG_TYPEDOBJECT_HANDLE_UNATTACHED);

  var ptr = TypedObjectPointer.fromTypedDatum(handle);
  return ptr.get();
}

// Handle.set: user exposed!
function HandleSet(handle, value) {
  if (!ObjectIsTypedHandle(handle))
    ThrowError(JSMSG_INCOMPATIBLE_PROTO, "Handle", "set", typeof value);

  if (!ObjectIsAttached(handle))
    ThrowError(JSMSG_TYPEDOBJECT_HANDLE_UNATTACHED);

  var ptr = TypedObjectPointer.fromTypedDatum(handle);
  ptr.set(value);
}

// Handle.isHandle: user exposed!
function HandleTest(obj) {
  return IsObject(obj) && ObjectIsTypedHandle(obj);
}

///////////////////////////////////////////////////////////////////////////
// Miscellaneous

// Warning: user exposed!
function TypeOfTypedDatum(obj) {
  if (!IsObject(obj) || !ObjectIsTypedDatum(obj))
    ThrowError(JSMSG_TYPEDOBJECT_HANDLE_BAD_ARGS,
               1, "typed object");

  return TYPED_TYPE_OBJ(obj);
}

function ObjectIsTypedDatum(obj) {
  return ObjectIsTypedObject(obj) || ObjectIsTypedHandle(obj);
}

function ObjectIsAttached(obj) {
  assert(ObjectIsTypedDatum(obj),
         "ObjectIsAttached() invoked on invalid obj");
  return TYPED_OWNER(obj) != null;
}
