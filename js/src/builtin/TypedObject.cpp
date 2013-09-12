/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "builtin/TypedObject.h"

#include "jscompartment.h"
#include "jsfun.h"
#include "jsobj.h"
#include "jsutil.h"

#include "builtin/TypeRepresentation.h"
#include "gc/Marking.h"
#include "js/Vector.h"
#include "vm/GlobalObject.h"
#include "vm/ObjectImpl.h"
#include "vm/String.h"
#include "vm/StringBuffer.h"
#include "vm/TypedArrayObject.h"

#include "jsatominlines.h"
#include "jsobjinlines.h"

#define NORMAL_JS_PROPS (JSPROP_ENUMERATE |                                   \
                         JSPROP_READONLY |                                    \
                         JSPROP_PERMANENT)

using mozilla::DebugOnly;

using namespace js;

Class js::TypedObjectClass = {
    "TypedObject",
    JSCLASS_HAS_CACHED_PROTO(JSProto_TypedObject),
    JS_PropertyStub,         /* addProperty */
    JS_DeletePropertyStub,   /* delProperty */
    JS_PropertyStub,         /* getProperty */
    JS_StrictPropertyStub,   /* setProperty */
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub
};

static void
ReportCannotConvertTo(JSContext *cx, HandleValue fromValue, const char *toType)
{
    JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_CANT_CONVERT_TO,
                         InformalValueTypeName(fromValue), toType);
}

static void
ReportCannotConvertTo(JSContext *cx, HandleObject fromObject, const char *toType)
{
    RootedValue fromValue(cx, ObjectValue(*fromObject));
    ReportCannotConvertTo(cx, fromValue, toType);
}

static void
ReportCannotConvertTo(JSContext *cx, HandleValue fromValue, HandleString toType)
{
    const char *fnName = JS_EncodeString(cx, toType);
    if (!fnName)
        return;
    ReportCannotConvertTo(cx, fromValue, fnName);
    JS_free(cx, (void *) fnName);
}

static void
ReportCannotConvertTo(JSContext *cx, HandleValue fromValue, TypeRepresentation *toType)
{
    StringBuffer contents(cx);
    if (!toType->appendString(cx, contents))
        return;

    RootedString result(cx, contents.finishString());
    if (!result)
        return;

    ReportCannotConvertTo(cx, fromValue, result);
}

static int32_t
Clamp(int32_t value, int32_t min, int32_t max)
{
    JS_ASSERT(min < max);
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

static inline JSObject *
ToObjectIfObject(HandleValue value)
{
    if (!value.isObject())
        return NULL;

    return &value.toObject();
}

static inline bool
IsNumericTypeObject(JSObject &type)
{
    return &NumericTypeClasses[0] <= type.getClass() &&
           type.getClass() < &NumericTypeClasses[ScalarTypeRepresentation::TYPE_MAX];
}

static inline bool
IsArrayTypeObject(JSObject &type)
{
    return type.hasClass(&ArrayType::class_);
}

static inline bool
IsStructTypeObject(JSObject &type)
{
    return type.hasClass(&StructType::class_);
}

static inline bool
IsComplexTypeObject(JSObject &type)
{
    return IsArrayTypeObject(type) || IsStructTypeObject(type);
}

static inline bool
IsTypeObject(JSObject &type)
{
    return IsNumericTypeObject(type) || IsComplexTypeObject(type);
}

static inline bool
HasTypedContents(JSObject &obj)
{
    return obj.is<TypedObject>() || obj.is<TypedHandle>();
}

static inline uint8_t *
TypedMem(JSObject &val)
{
    JS_ASSERT(HasTypedContents(val));
    return (uint8_t*) val.getPrivate();
}

/*
 * Given a user-visible type descriptor object, returns the
 * owner object for the TypeRepresentation* that we use internally.
 */
static JSObject *
typeRepresentationOwnerObj(JSObject &typeObj)
{
    JS_ASSERT(IsTypeObject(typeObj));
    return &typeObj.getReservedSlot(JS_TYPEOBJ_SLOT_TYPE_REPR).toObject();
}

/*
 * Given a user-visible type descriptor object, returns the
 * TypeRepresentation* that we use internally.
 *
 * Note: this pointer is valid only so long as `typeObj` remains rooted.
 */
static TypeRepresentation *
typeRepresentation(JSObject &typeObj)
{
    return TypeRepresentation::fromOwnerObject(*typeRepresentationOwnerObj(typeObj));
}

static inline JSObject *
GetType(JSObject &block)
{
    JS_ASSERT(HasTypedContents(block));
    return &block.getReservedSlot(JS_TYPEDOBJ_SLOT_TYPE_OBJ).toObject();
}

static inline js::PropertyName *
PropertyNameFromCString(JSContext *cx, const char *cstr)
{
    RootedAtom atom(cx, Atomize(cx, cstr, strlen(cstr)));
    if (!atom)
        return NULL;

    return atom->asPropertyName();
}

static JSFunction *
SelfHostedFunction(JSContext *cx, const char *name)
{
    RootedAtom atom(cx, Atomize(cx, name, strlen(name)));
    if (!atom)
        return NULL;

    RootedPropertyName propName(cx, atom->asPropertyName());
    RootedValue func(cx);
    if (!cx->global()->getIntrinsicValue(cx, propName, &func))
        return NULL;

    JS_ASSERT(func.isObject());
    JS_ASSERT(func.toObject().is<JSFunction>());
    return &func.toObject().as<JSFunction>();
}

/*
 * Overwrites the contents of `block` at offset `offset` with `val`
 * converted to the type `typeObj`
 */
static bool
ConvertAndCopyTo(JSContext *cx,
                 HandleObject typeObj,
                 HandleObject block,
                 int32_t offset,
                 HandleValue val)
{
    RootedFunction func(cx, SelfHostedFunction(cx, "ConvertAndCopyTo"));
    if (!func)
        return false;

    InvokeArgs args(cx);
    if (!args.init(5))
        return false;

    args.setCallee(ObjectValue(*func));
    args[0].setObject(*typeRepresentationOwnerObj(*typeObj));
    args[1].setObject(*typeObj);
    args[2].setObject(*block);
    args[3].setInt32(offset);
    args[4].set(val);

    return Invoke(cx, args);
}

static bool
ConvertAndCopyTo(JSContext *cx, HandleObject block, HandleValue val)
{
    RootedObject type(cx, GetType(*block));
    return ConvertAndCopyTo(cx, type, block, 0, val);
}

/*
 * Overwrites the contents of `block` at offset `offset` with `val`
 * converted to the type `typeObj`
 */
static bool
Reify(JSContext *cx,
      TypeRepresentation *typeRepr,
      HandleObject type,
      HandleObject owner,
      size_t offset,
      MutableHandleValue to)
{
    RootedFunction func(cx, SelfHostedFunction(cx, "Reify"));
    if (!func)
        return false;

    InvokeArgs args(cx);
    if (!args.init(4))
        return false;

    args.setCallee(ObjectValue(*func));
    args[0].setObject(*typeRepr->ownerObject());
    args[1].setObject(*type);
    args[2].setObject(*owner);
    args[3].setInt32(offset);

    if (!Invoke(cx, args))
        return false;

    to.set(args.rval());
    return true;
}

static inline size_t
TypeSize(JSObject &type)
{
    return typeRepresentation(type)->size();
}

static inline size_t
BlockSize(JSObject &val)
{
    return TypeSize(*GetType(val));
}

static inline bool
HasTypedContentsOfKind(JSObject &obj, TypeRepresentation::Kind kind)
{
    if (!HasTypedContents(obj))
        return false;
    TypeRepresentation *repr = typeRepresentation(*GetType(obj));
    return repr->kind() == kind;
}

static bool
TypeEquivalent(JSContext *cx, unsigned int argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    RootedObject thisObj(cx, ToObjectIfObject(args.thisv()));
    if (!thisObj || !IsTypeObject(*thisObj)) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                             JSMSG_INCOMPATIBLE_PROTO,
                             "Type", "equivalent",
                             InformalValueTypeName(args.thisv()));
        return false;
    }

    if (args.length() < 1) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_MORE_ARGS_NEEDED,
                             "Type.equivalent", "1", "s");
        return false;
    }

    RootedObject otherObj(cx, ToObjectIfObject(args[0]));
    if (!otherObj || !IsTypeObject(*otherObj)) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_TYPEDOBJECT_NOT_TYPE_OBJECT);
        return false;
    }

    TypeRepresentation *thisRepr = typeRepresentation(*thisObj);
    TypeRepresentation *otherRepr = typeRepresentation(*otherObj);
    args.rval().setBoolean(thisRepr == otherRepr);
    return true;
}

#define BINARYDATA_NUMERIC_CLASSES(constant_, type_, name_)                   \
{                                                                             \
    #name_,                                                                   \
    JSCLASS_HAS_RESERVED_SLOTS(JS_TYPEOBJ_SCALAR_SLOTS),                      \
    JS_PropertyStub,       /* addProperty */                                  \
    JS_DeletePropertyStub, /* delProperty */                                  \
    JS_PropertyStub,       /* getProperty */                                  \
    JS_StrictPropertyStub, /* setProperty */                                  \
    JS_EnumerateStub,                                                         \
    JS_ResolveStub,                                                           \
    JS_ConvertStub,                                                           \
    NULL,                                                                     \
    NULL,                                                                     \
    NumericType<constant_, type_>::call,                                      \
    NULL,                                                                     \
    NULL,                                                                     \
    NULL                                                                      \
},

Class js::NumericTypeClasses[ScalarTypeRepresentation::TYPE_MAX] = {
    JS_FOR_EACH_SCALAR_TYPE_REPR(BINARYDATA_NUMERIC_CLASSES)
};

template <typename T>
static T ConvertScalar(double d)
{
    if (TypeIsFloatingPoint<T>()) {
        return T(d);
    } else if (TypeIsUnsigned<T>()) {
        uint32_t n = ToUint32(d);
        return T(n);
    } else {
        int32_t n = ToInt32(d);
        return T(n);
    }
}

template<ScalarTypeRepresentation::Type N>
static bool
NumericTypeToString(JSContext *cx, unsigned int argc, Value *vp)
{
    JS_STATIC_ASSERT(N < ScalarTypeRepresentation::TYPE_MAX);
    CallArgs args = CallArgsFromVp(argc, vp);
    JSString *s = JS_NewStringCopyZ(cx, ScalarTypeRepresentation::typeName(N));
    if (!s)
        return false;
    args.rval().set(StringValue(s));
    return true;
}

template <ScalarTypeRepresentation::Type type, typename T>
bool
NumericType<type, T>::call(JSContext *cx, unsigned argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    if (args.length() < 1) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_MORE_ARGS_NEEDED,
                             args.callee().getClass()->name, "0", "s");
        return false;
    }

    double number;
    if (!ToNumber(cx, args[0], &number))
        return false;

    if (type == ScalarTypeRepresentation::TYPE_UINT8_CLAMPED)
        number = ClampDoubleToUint8(number);

    T converted = ConvertScalar<T>(number);
    args.rval().setNumber((double) converted);
    return true;
}

/*
 * For code like:
 *
 *   var A = new ArrayType(uint8, 10)
 *   var S = new StructType({...})
 *
 * then
 *
 *   A.prototype.__proto__ === ArrayType.prototype.prototype
 *   S.prototype.__proto__ === StructType.prototype.prototype
 *
 * This function takes a reference to either ArrayType or StructType and
 * returns a JSObject which can be set as A.prototype.
 */
JSObject *
CreatePrototypeObjectForComplexTypeInstance(JSContext *cx,
                                            HandleObject typeObjectCtor)
{
    RootedValue ctorPrototypeVal(cx);
    if (!JSObject::getProperty(cx, typeObjectCtor, typeObjectCtor,
                               cx->names().classPrototype,
                               &ctorPrototypeVal))
        return NULL;
    JS_ASSERT(ctorPrototypeVal.isObject()); // immutable binding
    RootedObject ctorPrototypeObj(cx, &ctorPrototypeVal.toObject());

    RootedValue ctorPrototypePrototypeVal(cx);
    if (!JSObject::getProperty(cx, ctorPrototypeObj, ctorPrototypeObj,
                               cx->names().classPrototype,
                               &ctorPrototypePrototypeVal))
        return NULL;

    JS_ASSERT(ctorPrototypePrototypeVal.isObject()); // immutable binding
    RootedObject proto(cx, &ctorPrototypePrototypeVal.toObject());
    return NewObjectWithGivenProto(cx, &JSObject::class_, proto, NULL);
}

template<typename T>
static JSObject *
InitClass(JSContext *cx,
          Handle<GlobalObject*> global)
{
    RootedAtom className(cx, Atomize(cx, T::class_.name,
                                     strlen(T::class_.name)));
    if (!className)
        return NULL;

    RootedObject funcProto(cx, global->getOrCreateFunctionPrototype(cx));
    if (!funcProto)
        return NULL;

    // Create ctor.prototype, which inherits from Function.__proto__

    RootedObject proto(
        cx, NewObjectWithGivenProto(cx, &JSObject::class_, funcProto,
                                    global, SingletonObject));
    if (!proto)
        return NULL;

    // Create ctor.prototype.prototype, which inherits from Object.__proto__

    RootedObject objProto(cx, global->getOrCreateObjectPrototype(cx));
    if (!objProto)
        return NULL;
    RootedObject protoProto(
        cx, NewObjectWithGivenProto(cx, &JSObject::class_, objProto,
                                    global, SingletonObject));
    if (!proto)
        return NULL;

    RootedValue protoProtoValue(cx, ObjectValue(*protoProto));
    if (!JSObject::defineProperty(cx, proto, cx->names().classPrototype,
                                  protoProtoValue,
                                  NULL, NULL,
                                  NORMAL_JS_PROPS))
        return NULL;

    // Create ctor itself

    RootedFunction ctor(cx,
                        global->createConstructor(cx, T::construct,
                                                  className, 2));
    if (!ctor ||
        !LinkConstructorAndPrototype(cx, ctor, proto) ||
        !DefinePropertiesAndBrand(cx, proto,
                                  T::typeObjectProperties,
                                  T::typeObjectMethods) ||
        !DefinePropertiesAndBrand(cx, protoProto,
                                  T::typedObjectProperties,
                                  T::typedObjectMethods))
    {
        return NULL;
    }

    return ctor;
}

Class ArrayType::class_ = {
    "ArrayType",
    JSCLASS_HAS_RESERVED_SLOTS(JS_TYPEOBJ_ARRAY_SLOTS),
    JS_PropertyStub,
    JS_DeletePropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    NULL,
    NULL,
    NULL,
    NULL,
    TypedObject::construct,
    NULL
};

const JSPropertySpec ArrayType::typeObjectProperties[] = {
    JS_PS_END
};

const JSFunctionSpec ArrayType::typeObjectMethods[] = {
    {"handle", {NULL, NULL}, 2, 0, "HandleCreate"},
    JS_FN("repeat", ArrayType::repeat, 1, 0),
    JS_FN("toSource", ArrayType::toSource, 0, 0),
    JS_FS_END
};

const JSPropertySpec ArrayType::typedObjectProperties[] = {
    JS_PS_END
};

const JSFunctionSpec ArrayType::typedObjectMethods[] = {
    JS_FN("subarray", ArrayType::subarray, 2, 0),
    {"forEach", {NULL, NULL}, 1, 0, "ArrayForEach"},
    JS_FS_END
};

static JSObject *
ArrayElementType(HandleObject array)
{
    JS_ASSERT(IsArrayTypeObject(*array));
    return &array->getReservedSlot(JS_TYPEOBJ_SLOT_ARRAY_ELEM_TYPE).toObject();
}

static bool
FillTypedArrayWithValue(JSContext *cx, HandleObject array, HandleValue val)
{
    JS_ASSERT(HasTypedContentsOfKind(*array, TypeRepresentation::Array));

    RootedFunction func(
        cx, SelfHostedFunction(cx, "FillTypedArrayWithValue"));
    if (!func)
        return false;

    InvokeArgs args(cx);
    if (!args.init(2))
        return false;

    args.setCallee(ObjectValue(*func));
    args[0].setObject(*array);
    args[1].set(val);
    return Invoke(cx, args);
}

bool
ArrayType::repeat(JSContext *cx, unsigned int argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    if (args.length() < 1) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage,
                             NULL, JSMSG_MORE_ARGS_NEEDED,
                             "repeat()", "0", "s");
        return false;
    }

    RootedObject thisObj(cx, ToObjectIfObject(args.thisv()));
    if (!thisObj || !IsArrayTypeObject(*thisObj)) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                             JSMSG_INCOMPATIBLE_PROTO,
                             "ArrayType", "repeat",
                             InformalValueTypeName(args.thisv()));
        return false;
    }

    RootedObject binaryArray(cx, TypedObject::createZeroed(cx, thisObj));
    if (!binaryArray)
        return false;

    RootedValue val(cx, args[0]);
    if (!FillTypedArrayWithValue(cx, binaryArray, val))
        return false;

    args.rval().setObject(*binaryArray);
    return true;
}

bool
ArrayType::toSource(JSContext *cx, unsigned int argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    RootedObject thisObj(cx, ToObjectIfObject(args.thisv()));
    if (!thisObj || !IsArrayTypeObject(*thisObj)) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage,
                             NULL, JSMSG_INCOMPATIBLE_PROTO,
                             "ArrayType", "toSource",
                             InformalValueTypeName(args.thisv()));
        return false;
    }

    StringBuffer contents(cx);
    if (!typeRepresentation(*thisObj)->appendString(cx, contents))
        return false;

    RootedString result(cx, contents.finishString());
    if (!result)
        return false;

    args.rval().setString(result);
    return true;
}

/**
 * The subarray function first creates an ArrayType instance
 * which will act as the elementType for the subarray.
 *
 * var MA = new ArrayType(elementType, 10);
 * var mb = MA.repeat(val);
 *
 * mb.subarray(begin, end=mb.length) => (Only for +ve)
 *     var internalSA = new ArrayType(elementType, end-begin);
 *     var ret = new internalSA()
 *     for (var i = begin; i < end; i++)
 *         ret[i-begin] = ret[i]
 *     return ret
 *
 * The range specified by the begin and end values is clamped to the valid
 * index range for the current array. If the computed length of the new
 * TypedArray would be negative, it is clamped to zero.
 * see: http://www.khronos.org/registry/typedarray/specs/latest/#7
 *
 */
/*static*/ bool
ArrayType::subarray(JSContext *cx, unsigned int argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    if (args.length() < 1) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage,
                             NULL, JSMSG_MORE_ARGS_NEEDED,
                             "subarray()", "0", "s");
        return false;
    }

    if (!args[0].isInt32()) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                             JSMSG_TYPEDOBJECT_SUBARRAY_INTEGER_ARG, "1");
        return false;
    }

    RootedObject thisObj(cx, &args.thisv().toObject());
    if (!HasTypedContentsOfKind(*thisObj, TypeRepresentation::Array)) {
        ReportCannotConvertTo(cx, thisObj, "binary array");
        return false;
    }

    RootedObject type(cx, GetType(*thisObj));
    ArrayTypeRepresentation *typeRepr = typeRepresentation(*type)->asArray();
    size_t length = typeRepr->length();

    int32_t begin = args[0].toInt32();
    int32_t end = length;

    if (args.length() >= 2) {
        if (!args[1].isInt32()) {
            JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                    JSMSG_TYPEDOBJECT_SUBARRAY_INTEGER_ARG, "2");
            return false;
        }

        end = args[1].toInt32();
    }

    if (begin < 0)
        begin = length + begin;
    if (end < 0)
        end = length + end;

    begin = Clamp(begin, 0, length);
    end = Clamp(end, 0, length);

    int32_t sublength = end - begin; // end exclusive
    sublength = Clamp(sublength, 0, length);

    RootedObject globalObj(cx, cx->compartment()->maybeGlobal());
    JS_ASSERT(globalObj);
    Rooted<GlobalObject*> global(cx, &globalObj->as<GlobalObject>());
    RootedObject arrayTypeGlobal(cx, global->getArrayType(cx));

    RootedObject elementType(cx, ArrayElementType(type));
    RootedObject subArrayType(cx, ArrayType::create(cx, arrayTypeGlobal,
                                                    elementType, sublength));
    if (!subArrayType)
        return false;

    int32_t elementSize = typeRepr->element()->size();
    size_t offset = elementSize * begin;

    RootedObject subarray(
        cx, TypedContents::createDerived(cx, subArrayType, thisObj, offset));
    if (!subarray)
        return false;

    args.rval().setObject(*subarray);
    return true;
}

static bool
ArrayFillSubarray(JSContext *cx, unsigned int argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    if (args.length() < 1) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage,
                             NULL, JSMSG_MORE_ARGS_NEEDED,
                             "fill()", "0", "s");
        return false;
    }

    RootedObject thisObj(cx, ToObjectIfObject(args.thisv()));
    if (!thisObj || !HasTypedContentsOfKind(*thisObj, TypeRepresentation::Array)) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage,
                             NULL, JSMSG_INCOMPATIBLE_PROTO,
                             "ArrayType", "fill",
                             InformalValueTypeName(args.thisv()));
        return false;
    }

    Value funArrayTypeVal = GetFunctionNativeReserved(&args.callee(), 0);
    JS_ASSERT(funArrayTypeVal.isObject());

    RootedObject type(cx, GetType(*thisObj));
    TypeRepresentation *typeRepr = typeRepresentation(*type);
    RootedObject funArrayType(cx, &funArrayTypeVal.toObject());
    TypeRepresentation *funArrayTypeRepr = typeRepresentation(*funArrayType);
    if (typeRepr != funArrayTypeRepr) {
        RootedValue thisObjValue(cx, ObjectValue(*thisObj));
        ReportCannotConvertTo(cx, thisObjValue, funArrayTypeRepr);
        return false;
    }

    args.rval().setUndefined();
    RootedValue val(cx, args[0]);
    return FillTypedArrayWithValue(cx, thisObj, val);
}

static bool
InitializeCommonTypeDescriptorProperties(JSContext *cx,
                                         HandleObject obj,
                                         HandleObject typeReprOwnerObj)
{
    TypeRepresentation *typeRepr =
        TypeRepresentation::fromOwnerObject(*typeReprOwnerObj);

    // equivalent()
    if (!JS_DefineFunction(cx, obj, "equivalent",
                           TypeEquivalent, 1, 0))
        return false;

    // byteLength
    RootedValue typeByteLength(cx, NumberValue(typeRepr->size()));
    if (!JSObject::defineProperty(cx, obj, cx->names().byteLength,
                                  typeByteLength,
                                  NULL, NULL,
                                  NORMAL_JS_PROPS))
        return false;

    // byteAlignment
    RootedValue typeByteAlignment(cx, NumberValue(typeRepr->alignment()));
    if (!JSObject::defineProperty(cx, obj, cx->names().byteAlignment,
                                  typeByteAlignment,
                                  NULL, NULL,
                                  NORMAL_JS_PROPS))
        return false;

    // variable -- always false since we do not yet support variable-size types
    RootedValue variable(cx, JSVAL_FALSE);
    if (!JSObject::defineProperty(cx, obj, cx->names().variable,
                                  variable,
                                  NULL, NULL,
                                  NORMAL_JS_PROPS))
        return false;

    return true;
}

JSObject *
ArrayType::create(JSContext *cx,
                  HandleObject metaTypeObject,
                  HandleObject elementType,
                  size_t length)
{
    JS_ASSERT(elementType);
    JS_ASSERT(IsTypeObject(*elementType));

    TypeRepresentation *elementTypeRepr = typeRepresentation(*elementType);
    RootedObject typeReprObj(
        cx, ArrayTypeRepresentation::Create(cx, elementTypeRepr, length));
    if (!typeReprObj)
        return NULL;

    RootedValue prototypeVal(cx);
    if (!JSObject::getProperty(cx, metaTypeObject, metaTypeObject,
                               cx->names().classPrototype,
                               &prototypeVal))
        return NULL;
    JS_ASSERT(prototypeVal.isObject()); // immutable binding

    RootedObject obj(
        cx, NewObjectWithClassProto(cx, &ArrayType::class_,
                                    &prototypeVal.toObject(), NULL));
    if (!obj)
        return NULL;
    obj->initReservedSlot(JS_TYPEOBJ_SLOT_TYPE_REPR,
                          ObjectValue(*typeReprObj));

    RootedValue elementTypeVal(cx, ObjectValue(*elementType));
    if (!JSObject::defineProperty(cx, obj, cx->names().elementType,
                                  elementTypeVal, NULL, NULL,
                                  NORMAL_JS_PROPS))
        return NULL;

    obj->initReservedSlot(JS_TYPEOBJ_SLOT_ARRAY_ELEM_TYPE,
                          elementTypeVal);

    RootedValue lengthVal(cx, Int32Value(length));
    if (!JSObject::defineProperty(cx, obj, cx->names().length,
                                  lengthVal, NULL, NULL,
                                  NORMAL_JS_PROPS))
        return NULL;

    if (!InitializeCommonTypeDescriptorProperties(cx, obj, typeReprObj))
        return NULL;

    RootedObject prototypeObj(
        cx, CreatePrototypeObjectForComplexTypeInstance(cx, metaTypeObject));
    if (!prototypeObj)
        return NULL;

    if (!LinkConstructorAndPrototype(cx, obj, prototypeObj))
        return NULL;

    JSFunction *fillFun = DefineFunctionWithReserved(cx, prototypeObj, "fill", ArrayFillSubarray, 1, 0);
    if (!fillFun)
        return NULL;

    // This is important
    // so that A.prototype.fill.call(b, val)
    // where b.type != A raises an error
    SetFunctionNativeReserved(fillFun, 0, ObjectValue(*obj));

    return obj;
}

bool
ArrayType::construct(JSContext *cx, unsigned argc, Value *vp)
{
    if (!JS_IsConstructing(cx, vp)) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                             JSMSG_NOT_FUNCTION, "ArrayType");
        return false;
    }

    CallArgs args = CallArgsFromVp(argc, vp);

    if (argc != 2 ||
        !args[0].isObject() ||
        !args[1].isNumber() ||
        args[1].toNumber() < 0)
    {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                             JSMSG_TYPEDOBJECT_ARRAYTYPE_BAD_ARGS);
        return false;
    }

    RootedObject arrayTypeGlobal(cx, &args.callee());
    RootedObject elementType(cx, &args[0].toObject());

    if (!IsTypeObject(*elementType)) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                             JSMSG_TYPEDOBJECT_ARRAYTYPE_BAD_ARGS);
        return false;
    }

    if (!args[1].isInt32()) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                             JSMSG_TYPEDOBJECT_ARRAYTYPE_BAD_ARGS);
        return false;
    }

    int32_t length = args[1].toInt32();
    if (length < 0) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                             JSMSG_TYPEDOBJECT_ARRAYTYPE_BAD_ARGS);
        return false;
    }

    RootedObject obj(cx, create(cx, arrayTypeGlobal, elementType, length));
    if (!obj)
        return false;
    args.rval().setObject(*obj);
    return true;
}

/*********************************
 * Structs
 *********************************/

Class StructType::class_ = {
    "StructType",
    JSCLASS_HAS_RESERVED_SLOTS(JS_TYPEOBJ_STRUCT_SLOTS) |
    JSCLASS_HAS_PRIVATE, // used to store FieldList
    JS_PropertyStub,
    JS_DeletePropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    NULL, /* finalize */
    NULL, /* checkAccess */
    NULL, /* call */
    NULL, /* hasInstance */
    TypedObject::construct,
    NULL  /* trace */
};

const JSPropertySpec StructType::typeObjectProperties[] = {
    JS_PS_END
};

const JSFunctionSpec StructType::typeObjectMethods[] = {
    {"handle", {NULL, NULL}, 2, 0, "HandleCreate"},
    JS_FN("toSource", StructType::toSource, 0, 0),
    JS_FS_END
};

const JSPropertySpec StructType::typedObjectProperties[] = {
    JS_PS_END
};

const JSFunctionSpec StructType::typedObjectMethods[] = {
    JS_FS_END
};

/*
 * NOTE: layout() does not check for duplicates in fields since the arguments
 * to StructType are currently passed as an object literal. Fix this if it
 * changes to taking an array of arrays.
 */
bool
StructType::layout(JSContext *cx, HandleObject structType, HandleObject fields)
{
    AutoIdVector ids(cx);
    if (!GetPropertyNames(cx, fields, JSITER_OWNONLY, &ids))
        return false;

    AutoValueVector fieldTypeObjs(cx);
    AutoObjectVector fieldTypeReprObjs(cx);

    RootedValue fieldTypeVal(cx);
    RootedId id(cx);
    for (unsigned int i = 0; i < ids.length(); i++) {
        id = ids[i];

        if (!JSObject::getGeneric(cx, fields, fields, id, &fieldTypeVal))
            return false;

        uint32_t index;
        if (js_IdIsIndex(id, &index)) {
            RootedValue idValue(cx, IdToJsval(id));
            ReportCannotConvertTo(cx, idValue, "StructType field name");
            return false;
        }

        RootedObject fieldType(cx, ToObjectIfObject(fieldTypeVal));
        if (!fieldType || !IsTypeObject(*fieldType)) {
            ReportCannotConvertTo(cx, fieldTypeVal, "StructType field specifier");
            return false;
        }

        if (!fieldTypeObjs.append(fieldTypeVal)) {
            js_ReportOutOfMemory(cx);
            return false;
        }

        if (!fieldTypeReprObjs.append(typeRepresentationOwnerObj(*fieldType))) {
            js_ReportOutOfMemory(cx);
            return false;
        }
    }

    // Construct the `TypeRepresentation*`.
    RootedObject typeReprObj(
        cx, StructTypeRepresentation::Create(cx, ids, fieldTypeReprObjs));
    if (!typeReprObj)
        return false;
    StructTypeRepresentation *typeRepr =
        TypeRepresentation::fromOwnerObject(*typeReprObj)->asStruct();
    structType->initReservedSlot(JS_TYPEOBJ_SLOT_TYPE_REPR,
                                 ObjectValue(*typeReprObj));

    // Construct for internal use an array with the type object for each field.
    RootedObject fieldTypeVec(
        cx, NewDenseCopiedArray(cx, fieldTypeObjs.length(),
                                fieldTypeObjs.begin()));
    if (!fieldTypeVec)
        return false;

    structType->initReservedSlot(JS_TYPEOBJ_SLOT_STRUCT_FIELD_TYPES,
                                 ObjectValue(*fieldTypeVec));

    // Construct the fieldNames vector
    AutoValueVector fieldNameValues(cx);
    for (unsigned int i = 0; i < ids.length(); i++) {
        RootedValue value(cx, IdToValue(ids[i]));
        if (!fieldNameValues.append(value))
            return false;
    }
    RootedObject fieldNamesVec(
        cx, NewDenseCopiedArray(cx, fieldNameValues.length(),
                                fieldNameValues.begin()));
    if (!fieldNamesVec)
        return false;
    RootedValue fieldNamesVecValue(cx, ObjectValue(*fieldNamesVec));
    if (!JSObject::defineProperty(cx, structType, cx->names().fieldNames,
                                  fieldNamesVecValue, NULL, NULL,
                                  NORMAL_JS_PROPS))
        return false;

    // Construct the fieldNames, fieldOffsets and fieldTypes objects:
    // fieldNames : [ string ]
    // fieldOffsets : { string: integer, ... }
    // fieldTypes : { string: Type, ... }
    RootedObject fieldOffsets(
        cx, NewObjectWithClassProto(cx, &JSObject::class_, NULL, NULL));
    RootedObject fieldTypes(
        cx, NewObjectWithClassProto(cx, &JSObject::class_, NULL, NULL));
    for (size_t i = 0; i < typeRepr->fieldCount(); i++) {
        const StructField &field = typeRepr->field(i);
        RootedId fieldId(cx, field.id);

        // fieldOffsets[id] = offset
        RootedValue offset(cx, NumberValue(field.offset));
        if (!JSObject::defineGeneric(cx, fieldOffsets, fieldId,
                                     offset, NULL, NULL,
                                     NORMAL_JS_PROPS))
            return false;

        // fieldTypes[id] = typeObj
        if (!JSObject::defineGeneric(cx, fieldTypes, fieldId,
                                     fieldTypeObjs.handleAt(i), NULL, NULL,
                                     NORMAL_JS_PROPS))
            return false;
    }

    RootedValue fieldOffsetsValue(cx, ObjectValue(*fieldOffsets));
    if (!JSObject::defineProperty(cx, structType, cx->names().fieldOffsets,
                                  fieldOffsetsValue, NULL, NULL,
                                  NORMAL_JS_PROPS))
        return false;

    RootedValue fieldTypesValue(cx, ObjectValue(*fieldTypes));
    if (!JSObject::defineProperty(cx, structType, cx->names().fieldTypes,
                                  fieldTypesValue, NULL, NULL,
                                  NORMAL_JS_PROPS))
        return false;

    return true;
}

JSObject *
StructType::create(JSContext *cx, HandleObject metaTypeObject,
                   HandleObject fields)
{
    RootedValue prototypeVal(cx);
    if (!JSObject::getProperty(cx, metaTypeObject, metaTypeObject,
                               cx->names().classPrototype,
                               &prototypeVal))
        return NULL;
    JS_ASSERT(prototypeVal.isObject()); // immutable binding

    RootedObject obj(
        cx, NewObjectWithClassProto(cx, &StructType::class_,
                                    &prototypeVal.toObject(), NULL));
    if (!obj)
        return NULL;

    if (!StructType::layout(cx, obj, fields))
        return NULL;

    RootedObject fieldsProto(cx);
    if (!JSObject::getProto(cx, fields, &fieldsProto))
        return NULL;

    RootedObject typeReprObj(cx, typeRepresentationOwnerObj(*obj));
    if (!InitializeCommonTypeDescriptorProperties(cx, obj, typeReprObj))
        return NULL;

    RootedObject prototypeObj(
        cx, CreatePrototypeObjectForComplexTypeInstance(cx, metaTypeObject));
    if (!prototypeObj)
        return NULL;

    if (!LinkConstructorAndPrototype(cx, obj, prototypeObj))
        return NULL;

    return obj;
}

bool
StructType::construct(JSContext *cx, unsigned int argc, Value *vp)
{
    if (!JS_IsConstructing(cx, vp)) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                             JSMSG_NOT_FUNCTION, "StructType");
        return false;
    }

    CallArgs args = CallArgsFromVp(argc, vp);

    if (argc >= 1 && args[0].isObject()) {
        RootedObject metaTypeObject(cx, &args.callee());
        RootedObject fields(cx, &args[0].toObject());
        RootedObject obj(cx, create(cx, metaTypeObject, fields));
        if (!obj)
            return false;
        args.rval().setObject(*obj);
        return true;
    }

    JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                         JSMSG_TYPEDOBJECT_STRUCTTYPE_BAD_ARGS);
    return false;
}

bool
StructType::toSource(JSContext *cx, unsigned int argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    RootedObject thisObj(cx, ToObjectIfObject(args.thisv()));
    if (!thisObj || !IsStructTypeObject(*thisObj)) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_INCOMPATIBLE_PROTO,
                             "StructType", "toSource",
                             InformalValueTypeName(args.thisv()));
        return false;
    }

    StringBuffer contents(cx);
    if (!typeRepresentation(*thisObj)->appendString(cx, contents))
        return false;

    RootedString result(cx, contents.finishString());
    if (!result)
        return false;

    args.rval().setString(result);
    return true;
}

///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////
// Creating the TypedObject "module"
//
// We create one global, `TypedObject`, which contains the following
// members:
//
// 1. uint8, uint16, etc
// 2. ArrayType
// 3. StructType
//
// Each of these is a function and hence their prototype is
// `Function.__proto__` (in terms of the JS Engine, they are not
// JSFunctions but rather instances of their own respective JSClasses
// which override the call and construct operations).
//
// Each type object also has its own `prototype` field. Therefore,
// using `StructType` as an example, the basic setup is:
//
//   StructType --__proto__--> Function.__proto__
//        |
//    prototype
//        |
//        v
//       { } -----__proto__--> Function.__proto__
//
// When a new type object (e.g., an instance of StructType) is created,
// it will look as follows:
//
//   MyStruct -__proto__-> StructType.prototype -__proto__-> Function.__proto__
//        |
//    prototype
//        |
//        v
//       { } --__proto__-> Object.prototype
//
// Finally, when an instance of `MyStruct` is created, its
// structure is as follows:
//
//    object ----__proto__--> MyStruct.prototype

template<ScalarTypeRepresentation::Type type>
static bool
DefineNumericClass(JSContext *cx, HandleObject global,
                   HandleObject module, const char *name);

JSObject *
js_InitTypedObjectClass(JSContext *cx, HandleObject obj)
{
    /*
     * The initialization strategy for TypedObjects is mildly unusual
     * compared to other classes. Because all of the types are members
     * of a single global, `TypedObject`, we basically make the
     * initialized for the `TypedObject` class populate the
     * `TypedObject` global (which is referred to as "module" herein).
     *
     * Note that the various TypedObject types, like ArrayType, uint8,
     * etc, are not entered into jsprototypes.h nor do they
     * participate in the usual initialization mechanisms. Instead
     * they are just eagerly created here. This is largely because
     * they do not define global names.
     */

    JS_ASSERT(obj->is<GlobalObject>());
    Rooted<GlobalObject *> global(cx, &obj->as<GlobalObject>());

    RootedObject objProto(cx, JS_GetObjectPrototype(cx, global));
    JS_ASSERT(objProto);

    RootedObject module(cx, NewObjectWithClassProto(cx, &JSObject::class_,
                                                    objProto, global));

    // Define TypedObject global.

    RootedValue moduleValue(cx, ObjectValue(*module));
    global->setReservedSlot(JSProto_TypedObject, moduleValue);
    if (!JSObject::defineProperty(cx, global, ClassName(JSProto_TypedObject, cx),
                                  moduleValue,
                                  NULL, NULL,
                                  0))
        return NULL;

    // uint8, uint16, etc

#define BINARYDATA_NUMERIC_DEFINE(constant_, type_, name_)                    \
    if (!DefineNumericClass<constant_>(cx, global, module, #name_))           \
        return NULL;
    JS_FOR_EACH_SCALAR_TYPE_REPR(BINARYDATA_NUMERIC_DEFINE)
#undef BINARYDATA_NUMERIC_DEFINE

    // ArrayType.

    RootedObject arrayType(cx, InitClass<ArrayType>(cx, global));
    if (!arrayType)
        return NULL;

    global->setArrayType(arrayType);

    RootedPropertyName arrayTypeName(
        cx, PropertyNameFromCString(cx, ArrayType::class_.name));
    if (!arrayTypeName)
        return NULL;

    RootedValue arrayTypeValue(cx, ObjectValue(*arrayType));
    if (!JSObject::defineProperty(cx, module, arrayTypeName,
                                  arrayTypeValue,
                                  NULL, NULL,
                                  NORMAL_JS_PROPS))
        return NULL;

    // StructType.

    RootedObject structType(cx, InitClass<StructType>(cx, global));
    if (!structType)
        return NULL;

    RootedPropertyName structTypeName(
        cx, PropertyNameFromCString(cx, StructType::class_.name));
    if (!structTypeName)
        return NULL;

    RootedValue structTypeValue(cx, ObjectValue(*structType));
    if (!JSObject::defineProperty(cx, module, structTypeName,
                                  structTypeValue,
                                  NULL, NULL,
                                  NORMAL_JS_PROPS))
        return NULL;

    //  Handle

    RootedObject handle(cx, NewBuiltinClassInstance(cx, &JSObject::class_));
    if (!module)
        return NULL;

    if (!JS_DefineFunctions(cx, handle, TypedHandle::handleStaticMethods))
        return NULL;

    RootedPropertyName handleTypeName(
        cx, PropertyNameFromCString(cx, TypedHandle::class_.name));
    if (!handleTypeName)
        return NULL;

    RootedValue handleValue(cx, ObjectValue(*handle));
    if (!JSObject::defineProperty(cx, module, handleTypeName,
                                  handleValue,
                                  NULL, NULL,
                                  NORMAL_JS_PROPS))
        return NULL;

    return module;
}

JSObject *
js_InitTypedObjectDummy(JSContext *cx, HandleObject obj)
{
    /*
     * This function is entered into the jsprototypes.h table
     * as the initializer for `TypedObject`. As far as I can tell,
     * though, this code never runs because instead the `TypedObject`
     * type is initialized via the `standard_class_atoms` mechanism.
     */

    MOZ_ASSUME_UNREACHABLE("Class should already be initialized");
}

template<ScalarTypeRepresentation::Type type>
static bool
DefineNumericClass(JSContext *cx,
                   HandleObject global,
                   HandleObject module,
                   const char *name)
{
    RootedObject funcProto(cx, JS_GetFunctionPrototype(cx, global));
    JS_ASSERT(funcProto);

    RootedObject numFun(cx, NewObjectWithClassProto(cx, &NumericTypeClasses[type],
                                                    funcProto, global));
    if (!numFun)
        return NULL;

    RootedObject typeReprObj(cx, ScalarTypeRepresentation::Create(cx, type));
    if (!typeReprObj)
        return false;

    numFun->initReservedSlot(JS_TYPEOBJ_SLOT_TYPE_REPR,
                             ObjectValue(*typeReprObj));

    if (!InitializeCommonTypeDescriptorProperties(cx, numFun, typeReprObj))
        return false;

    if (!JS_DefineFunction(cx, numFun, "toString",
                           NumericTypeToString<type>, 0, 0))
        return false;

    if (!JS_DefineFunction(cx, numFun, "toSource",
                           NumericTypeToString<type>, 0, 0))
        return false;

    RootedPropertyName className(cx, PropertyNameFromCString(cx, name));
    if (!className)
        return NULL;

    RootedValue numFunValue(cx, ObjectValue(*numFun));
    if (!JSObject::defineProperty(cx, module, className,
                                  numFunValue, NULL, NULL, 0))
        return NULL;

    return true;
}

///////////////////////////////////////////////////////////////////////////
// Binary blocks

template<class T>
/*static*/ T *
TypedContents::createUnattached(JSContext *cx,
                                HandleObject type)
{
    JS_STATIC_ASSERT(T::IsTypedContents);
    JS_ASSERT(IsTypeObject(*type));

    RootedObject obj(cx, createUnattachedWithClass(cx, &T::class_, type));
    if (!obj)
        return NULL;

    return &obj->as<T>();
}

/*static*/ TypedContents *
TypedContents::createUnattachedWithClass(JSContext *cx,
                                         Class *clasp,
                                         HandleObject type)
{
    JS_ASSERT(IsTypeObject(*type));
    JS_ASSERT(clasp == &TypedObject::class_ || clasp == &TypedHandle::class_);
    JS_ASSERT(JSCLASS_RESERVED_SLOTS(clasp) == JS_TYPEDOBJ_SLOTS);
    JS_ASSERT(clasp->hasPrivate());

    RootedValue protoVal(cx);
    if (!JSObject::getProperty(cx, type, type,
                               cx->names().classPrototype, &protoVal))
        return NULL;

    RootedObject obj(
        cx, NewObjectWithClassProto(cx, clasp, &protoVal.toObject(), NULL));
    if (!obj)
        return NULL;

    obj->setPrivate(NULL);
    obj->initReservedSlot(JS_TYPEDOBJ_SLOT_TYPE_OBJ, ObjectValue(*type));
    obj->initReservedSlot(JS_TYPEDOBJ_SLOT_OWNER, NullValue());

    // Tag the type object for this instance with the type
    // representation, if that has not been done already.
    if (cx->typeInferenceEnabled()) {
        RootedTypeObject typeObj(cx, obj->getType(cx));
        if (typeObj) {
            TypeRepresentation *typeRepr = typeRepresentation(*type);
            if (!typeObj->addTypedObjectAddendum(cx, typeRepr))
                return NULL;
        }
    }

    return static_cast<TypedContents*>(&*obj);
}

/*static*/ void
TypedContents::attach(void *memory)
{
    setPrivate(memory);
    setReservedSlot(JS_TYPEDOBJ_SLOT_OWNER, ObjectValue(*this));
}

/*static*/ void
TypedContents::attach(JSObject &owner, uint32_t offset)
{
    // `owner` must be attached
    JS_ASSERT(HasTypedContents(owner));
    JS_ASSERT(owner.getReservedSlot(JS_TYPEDOBJ_SLOT_OWNER).isObject());

    // find the location in memory
    uint8_t *mem = TypedMem(owner) + offset;

    // find the transitive owner, which is usually but not always `owner`
    JSObject &trueOwner = owner.getReservedSlot(JS_TYPEDOBJ_SLOT_OWNER).toObject();

    setPrivate(mem);
    setReservedSlot(JS_TYPEDOBJ_SLOT_OWNER, ObjectValue(trueOwner));
}

/*static*/ TypedContents *
TypedContents::createDerived(JSContext *cx, HandleObject type,
                             HandleObject owner, size_t offset)
{
    JS_ASSERT(HasTypedContents(*owner));
    JS_ASSERT(offset <= BlockSize(*owner));
    JS_ASSERT(offset + TypeSize(*type) <= BlockSize(*owner));

    js::Class *clasp = owner->getClass();
    Rooted<TypedContents*> obj(cx, createUnattachedWithClass(cx, clasp, type));
    if (!obj)
        return NULL;

    obj->attach(*owner, offset);
    return obj;
}

static bool
ReportBlockTypeError(JSContext *cx,
                     const unsigned errorNumber,
                     HandleObject obj)
{
    JS_ASSERT(HasTypedContents(*obj));

    RootedObject type(cx, GetType(*obj));
    TypeRepresentation *typeRepr = typeRepresentation(*type);

    StringBuffer contents(cx);
    if (!typeRepr->appendString(cx, contents))
        return false;

    RootedString result(cx, contents.finishString());
    if (!result)
        return false;

    char *typeReprStr = JS_EncodeString(cx, result.get());
    if (!typeReprStr)
        return false;

    JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                         errorNumber, typeReprStr);

    JS_free(cx, (void *) typeReprStr);
    return false;
}

/*static*/ void
TypedContents::obj_trace(JSTracer *trace, JSObject *object)
{
    JS_ASSERT(HasTypedContents(*object));

    for (size_t i = 0; i < JS_TYPEDOBJ_SLOTS; i++)
        gc::MarkSlot(trace, &object->getReservedSlotRef(i), "TypedObjectSlot");
}

/*static*/ void
TypedContents::obj_finalize(js::FreeOp *op, JSObject *obj)
{
    // if the obj owns the memory, indicating by the owner slot being
    // set to itself, then we must free it when finalized.

    JS_ASSERT(obj->getReservedSlotRef(JS_TYPEDOBJ_SLOT_OWNER).isObjectOrNull());

    if (obj->getReservedSlot(JS_TYPEDOBJ_SLOT_OWNER).isNull())
        return; // Unattached

    if (&obj->getReservedSlot(JS_TYPEDOBJ_SLOT_OWNER).toObject() == obj) {
        JS_ASSERT(obj->getPrivate());
        op->free_(obj->getPrivate());
    }
}

bool
TypedContents::obj_lookupGeneric(JSContext *cx, HandleObject obj, HandleId id,
                                MutableHandleObject objp, MutableHandleShape propp)
{
    JS_ASSERT(HasTypedContents(*obj));

    RootedObject type(cx, GetType(*obj));
    TypeRepresentation *typeRepr = typeRepresentation(*type);

    switch (typeRepr->kind()) {
      case TypeRepresentation::Scalar:
        break;

      case TypeRepresentation::Array: {
        uint32_t index;
        if (js_IdIsIndex(id, &index))
            return obj_lookupElement(cx, obj, index, objp, propp);

        if (JSID_IS_ATOM(id, cx->names().length)) {
            MarkNonNativePropertyFound(propp);
            objp.set(obj);
            return true;
        }
        break;
      }

      case TypeRepresentation::Struct: {
        RootedObject type(cx, GetType(*obj));
        JS_ASSERT(IsStructTypeObject(*type));
        StructTypeRepresentation *typeRepr =
            typeRepresentation(*type)->asStruct();
        const StructField *field = typeRepr->fieldNamed(id);
        if (field) {
            MarkNonNativePropertyFound(propp);
            objp.set(obj);
            return true;
        }
        break;
      }
    }

    RootedObject proto(cx, obj->getProto());
    if (!proto) {
        objp.set(NULL);
        propp.set(NULL);
        return true;
    }

    return JSObject::lookupGeneric(cx, proto, id, objp, propp);
}

bool
TypedContents::obj_lookupProperty(JSContext *cx,
                                HandleObject obj,
                                HandlePropertyName name,
                                MutableHandleObject objp,
                                MutableHandleShape propp)
{
    RootedId id(cx, NameToId(name));
    return obj_lookupGeneric(cx, obj, id, objp, propp);
}

bool
TypedContents::obj_lookupElement(JSContext *cx, HandleObject obj, uint32_t index,
                                MutableHandleObject objp, MutableHandleShape propp)
{
    JS_ASSERT(HasTypedContents(*obj));
    MarkNonNativePropertyFound(propp);
    objp.set(obj);
    return true;
}

static bool
ReportPropertyError(JSContext *cx,
                    const unsigned errorNumber,
                    HandleId id)
{
    RootedString str(cx, IdToString(cx, id));
    if (!str)
        return false;

    char *propName = JS_EncodeString(cx, str);
    if (!propName)
        return false;

    JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                         errorNumber, propName);

    JS_free(cx, propName);
    return false;
}

bool
TypedContents::obj_lookupSpecial(JSContext *cx, HandleObject obj,
                               HandleSpecialId sid, MutableHandleObject objp,
                               MutableHandleShape propp)
{
    RootedId id(cx, SPECIALID_TO_JSID(sid));
    return obj_lookupGeneric(cx, obj, id, objp, propp);
}

bool
TypedContents::obj_defineGeneric(JSContext *cx, HandleObject obj, HandleId id, HandleValue v,
                               PropertyOp getter, StrictPropertyOp setter, unsigned attrs)
{
    return ReportPropertyError(cx, JSMSG_UNDEFINED_PROP, id);
}

bool
TypedContents::obj_defineProperty(JSContext *cx, HandleObject obj,
                                HandlePropertyName name, HandleValue v,
                                PropertyOp getter, StrictPropertyOp setter, unsigned attrs)
{
    Rooted<jsid> id(cx, NameToId(name));
    return obj_defineGeneric(cx, obj, id, v, getter, setter, attrs);
}

bool
TypedContents::obj_defineElement(JSContext *cx, HandleObject obj, uint32_t index, HandleValue v,
                               PropertyOp getter, StrictPropertyOp setter, unsigned attrs)
{
    AutoRooterGetterSetter gsRoot(cx, attrs, &getter, &setter);

    RootedObject delegate(cx, ArrayBufferDelegate(cx, obj));
    if (!delegate)
        return false;
    return baseops::DefineElement(cx, delegate, index, v, getter, setter, attrs);
}

bool
TypedContents::obj_defineSpecial(JSContext *cx, HandleObject obj, HandleSpecialId sid, HandleValue v,
                               PropertyOp getter, StrictPropertyOp setter, unsigned attrs)
{
    Rooted<jsid> id(cx, SPECIALID_TO_JSID(sid));
    return obj_defineGeneric(cx, obj, id, v, getter, setter, attrs);
}

static JSObject *
StructFieldType(JSContext *cx,
                HandleObject type,
                int32_t fieldIndex)
{
    JS_ASSERT(IsStructTypeObject(*type));

    // Recover the original type object here (`field` contains
    // only its canonical form). The difference is observable,
    // e.g. in a program like:
    //
    //     var Point1 = new StructType({x:uint8, y:uint8});
    //     var Point2 = new StructType({x:uint8, y:uint8});
    //     var Line1 = new StructType({start:Point1, end: Point1});
    //     var Line2 = new StructType({start:Point2, end: Point2});
    //     var line1 = new Line1(...);
    //     var line2 = new Line2(...);
    //
    // In this scenario, line1.start.type() === Point1 and
    // line2.start.type() === Point2.
    RootedObject fieldTypes(
        cx,
        &type->getReservedSlot(JS_TYPEOBJ_SLOT_STRUCT_FIELD_TYPES).toObject());
    RootedValue fieldTypeVal(cx);
    if (!JSObject::getElement(cx, fieldTypes, fieldTypes,
                              fieldIndex, &fieldTypeVal))
        return NULL;

    return &fieldTypeVal.toObject();
}

bool
TypedContents::obj_getGeneric(JSContext *cx, HandleObject obj, HandleObject receiver,
                             HandleId id, MutableHandleValue vp)
{
    JS_ASSERT(HasTypedContents(*obj));

    // Dispatch elements to obj_getElement:
    uint32_t index;
    if (js_IdIsIndex(id, &index))
        return obj_getElement(cx, obj, receiver, index, vp);

    // Handle everything else here:

    RootedObject type(cx, GetType(*obj));
    TypeRepresentation *typeRepr = typeRepresentation(*type);
    switch (typeRepr->kind()) {
      case TypeRepresentation::Scalar:
        break;

      case TypeRepresentation::Array:
        if (JSID_IS_ATOM(id, cx->names().length)) {
            vp.setInt32(typeRepr->asArray()->length());
            return true;
        }
        break;

      case TypeRepresentation::Struct: {
        StructTypeRepresentation *structTypeRepr = typeRepr->asStruct();
        const StructField *field = structTypeRepr->fieldNamed(id);
        if (!field)
            break;

        RootedObject fieldType(cx, StructFieldType(cx, type, field->index));
        if (!fieldType)
            return false;

        return Reify(cx, field->typeRepr, fieldType, obj, field->offset, vp);
      }
    }

    RootedObject proto(cx, obj->getProto());
    if (!proto) {
        vp.setUndefined();
        return true;
    }

    return JSObject::getGeneric(cx, proto, receiver, id, vp);
}

bool
TypedContents::obj_getProperty(JSContext *cx, HandleObject obj, HandleObject receiver,
                              HandlePropertyName name, MutableHandleValue vp)
{
    RootedId id(cx, NameToId(name));
    return obj_getGeneric(cx, obj, receiver, id, vp);
}

bool
TypedContents::obj_getElement(JSContext *cx, HandleObject obj, HandleObject receiver,
                             uint32_t index, MutableHandleValue vp)
{
    bool present;
    return obj_getElementIfPresent(cx, obj, receiver, index, vp, &present);
}

bool
TypedContents::obj_getElementIfPresent(JSContext *cx, HandleObject obj,
                                     HandleObject receiver, uint32_t index,
                                     MutableHandleValue vp, bool *present)
{
    RootedObject type(cx, GetType(*obj));
    TypeRepresentation *typeRepr = typeRepresentation(*type);

    switch (typeRepr->kind()) {
      case TypeRepresentation::Scalar:
      case TypeRepresentation::Struct:
        break;

      case TypeRepresentation::Array: {
        *present = true;
        ArrayTypeRepresentation *arrayTypeRepr = typeRepr->asArray();

        if (index >= arrayTypeRepr->length()) {
            vp.setUndefined();
            return true;
        }

        RootedObject elementType(cx, ArrayElementType(type));
        size_t offset = arrayTypeRepr->element()->size() * index;
        return Reify(cx, arrayTypeRepr->element(), elementType, obj, offset, vp);
      }
    }

    RootedObject proto(cx, obj->getProto());
    if (!proto) {
        *present = false;
        vp.setUndefined();
        return true;
    }

    return JSObject::getElementIfPresent(cx, proto, receiver, index, vp, present);
}

bool
TypedContents::obj_getSpecial(JSContext *cx, HandleObject obj,
                            HandleObject receiver, HandleSpecialId sid,
                            MutableHandleValue vp)
{
    RootedId id(cx, SPECIALID_TO_JSID(sid));
    return obj_getGeneric(cx, obj, receiver, id, vp);
}

bool
TypedContents::obj_setGeneric(JSContext *cx, HandleObject obj, HandleId id,
                            MutableHandleValue vp, bool strict)
{
    uint32_t index;
    if (js_IdIsIndex(id, &index))
        return obj_setElement(cx, obj, index, vp, strict);

    RootedObject type(cx, GetType(*obj));
    TypeRepresentation *typeRepr = typeRepresentation(*type);

    switch (typeRepr->kind()) {
      case ScalarTypeRepresentation::Scalar:
        break;

      case ScalarTypeRepresentation::Array:
        if (JSID_IS_ATOM(id, cx->names().length)) {
            JS_ReportErrorNumber(cx, js_GetErrorMessage,
                                 NULL, JSMSG_CANT_REDEFINE_ARRAY_LENGTH);
            return false;
        }
        break;

      case ScalarTypeRepresentation::Struct: {
        const StructField *field = typeRepr->asStruct()->fieldNamed(id);
        if (!field)
            break;

        RootedObject fieldType(cx, StructFieldType(cx, type, field->index));
        if (!fieldType)
            return false;

        return ConvertAndCopyTo(cx, fieldType, obj, field->offset, vp);
      }
    }

    return ReportBlockTypeError(cx, JSMSG_OBJECT_NOT_EXTENSIBLE, obj);
}

bool
TypedContents::obj_setProperty(JSContext *cx, HandleObject obj,
                             HandlePropertyName name, MutableHandleValue vp,
                             bool strict)
{
    RootedId id(cx, NameToId(name));
    return obj_setGeneric(cx, obj, id, vp, strict);
}

bool
TypedContents::obj_setElement(JSContext *cx, HandleObject obj, uint32_t index,
                            MutableHandleValue vp, bool strict)
{
    RootedObject type(cx, GetType(*obj));
    TypeRepresentation *typeRepr = typeRepresentation(*type);

    switch (typeRepr->kind()) {
      case ScalarTypeRepresentation::Scalar:
      case ScalarTypeRepresentation::Struct:
        break;

      case ScalarTypeRepresentation::Array: {
        ArrayTypeRepresentation *arrayTypeRepr = typeRepr->asArray();

        if (index >= arrayTypeRepr->length()) {
            JS_ReportErrorNumber(cx, js_GetErrorMessage,
                                 NULL, JSMSG_TYPEDOBJECT_BINARYARRAY_BAD_INDEX);
            return false;
        }

        RootedObject elementType(cx, ArrayElementType(type));
        size_t offset = arrayTypeRepr->element()->size() * index;
        return ConvertAndCopyTo(cx, elementType, obj, offset, vp);
      }
    }

    return ReportBlockTypeError(cx, JSMSG_OBJECT_NOT_EXTENSIBLE, obj);
}

bool
TypedContents::obj_setSpecial(JSContext *cx, HandleObject obj,
                             HandleSpecialId sid, MutableHandleValue vp,
                             bool strict)
{
    RootedId id(cx, SPECIALID_TO_JSID(sid));
    return obj_setGeneric(cx, obj, id, vp, strict);
}

bool
TypedContents::obj_getGenericAttributes(JSContext *cx, HandleObject obj,
                                       HandleId id, unsigned *attrsp)
{
    uint32_t index;
    RootedObject type(cx, GetType(*obj));
    TypeRepresentation *typeRepr = typeRepresentation(*type);

    switch (typeRepr->kind()) {
      case TypeRepresentation::Scalar:
        break;

      case TypeRepresentation::Array:
        if (js_IdIsIndex(id, &index)) {
            *attrsp = JSPROP_ENUMERATE | JSPROP_PERMANENT;
            return true;
        }
        if (JSID_IS_ATOM(id, cx->names().length)) {
            *attrsp = JSPROP_READONLY | JSPROP_PERMANENT;
            return true;
        }
        break;

      case TypeRepresentation::Struct:
        if (typeRepr->asStruct()->fieldNamed(id) != NULL) {
            *attrsp = JSPROP_ENUMERATE | JSPROP_PERMANENT;
            return true;
        }
        break;
    }

    RootedObject proto(cx, obj->getProto());
    if (!proto) {
        *attrsp = 0;
        return true;
    }

    return JSObject::getGenericAttributes(cx, proto, id, attrsp);
}

static bool
IsOwnId(JSContext *cx, HandleObject obj, HandleId id)
{
    uint32_t index;
    RootedObject type(cx, GetType(*obj));
    TypeRepresentation *typeRepr = typeRepresentation(*type);

    switch (typeRepr->kind()) {
      case TypeRepresentation::Scalar:
        return false;

      case TypeRepresentation::Array:
        return js_IdIsIndex(id, &index) || JSID_IS_ATOM(id, cx->names().length);

      case TypeRepresentation::Struct:
        return typeRepr->asStruct()->fieldNamed(id) != NULL;
    }

    return false;
}

bool
TypedContents::obj_setGenericAttributes(JSContext *cx, HandleObject obj,
                                       HandleId id, unsigned *attrsp)
{
    RootedObject type(cx, GetType(*obj));

    if (IsOwnId(cx, obj, id))
        return ReportPropertyError(cx, JSMSG_CANT_REDEFINE_PROP, id);

    RootedObject proto(cx, obj->getProto());
    if (!proto) {
        *attrsp = 0;
        return true;
    }

    return JSObject::setGenericAttributes(cx, proto, id, attrsp);
}

bool
TypedContents::obj_deleteProperty(JSContext *cx, HandleObject obj,
                                HandlePropertyName name, bool *succeeded)
{
    Rooted<jsid> id(cx, NameToId(name));
    if (IsOwnId(cx, obj, id))
        return ReportPropertyError(cx, JSMSG_CANT_DELETE, id);

    RootedObject proto(cx, obj->getProto());
    if (!proto) {
        *succeeded = false;
        return true;
    }

    return JSObject::deleteProperty(cx, proto, name, succeeded);
}

bool
TypedContents::obj_deleteElement(JSContext *cx, HandleObject obj, uint32_t index,
                               bool *succeeded)
{
    RootedId id(cx);
    if (!IndexToId(cx, index, &id))
        return false;

    if (IsOwnId(cx, obj, id))
        return ReportPropertyError(cx, JSMSG_CANT_DELETE, id);

    RootedObject proto(cx, obj->getProto());
    if (!proto) {
        *succeeded = false;
        return true;
    }

    return JSObject::deleteElement(cx, proto, index, succeeded);
}

bool
TypedContents::obj_deleteSpecial(JSContext *cx, HandleObject obj,
                               HandleSpecialId sid, bool *succeeded)
{
    RootedObject proto(cx, obj->getProto());
    if (!proto) {
        *succeeded = false;
        return true;
    }

    return JSObject::deleteSpecial(cx, proto, sid, succeeded);
}

bool
TypedContents::obj_enumerate(JSContext *cx, HandleObject obj, JSIterateOp enum_op,
                           MutableHandleValue statep, MutableHandleId idp)
{
    uint32_t index;

    RootedObject type(cx, GetType(*obj));
    TypeRepresentation *typeRepr = typeRepresentation(*type);

    switch (typeRepr->kind()) {
      case TypeRepresentation::Scalar:
        switch (enum_op) {
          case JSENUMERATE_INIT_ALL:
          case JSENUMERATE_INIT:
            statep.setInt32(0);
            idp.set(INT_TO_JSID(0));

          case JSENUMERATE_NEXT:
          case JSENUMERATE_DESTROY:
            statep.setNull();
            break;
        }
        break;

      case TypeRepresentation::Array:
        switch (enum_op) {
          case JSENUMERATE_INIT_ALL:
          case JSENUMERATE_INIT:
            statep.setInt32(0);
            idp.set(INT_TO_JSID(typeRepr->asArray()->length()));
            break;

          case JSENUMERATE_NEXT:
            index = static_cast<int32_t>(statep.toInt32());

            if (index < typeRepr->asArray()->length()) {
                idp.set(INT_TO_JSID(index));
                statep.setInt32(index + 1);
            } else {
                JS_ASSERT(index == typeRepr->asArray()->length());
                statep.setNull();
            }

            break;

          case JSENUMERATE_DESTROY:
            statep.setNull();
            break;
        }
        break;

      case TypeRepresentation::Struct:
        switch (enum_op) {
          case JSENUMERATE_INIT_ALL:
          case JSENUMERATE_INIT:
            statep.setInt32(0);
            idp.set(INT_TO_JSID(typeRepr->asStruct()->fieldCount()));
            break;

          case JSENUMERATE_NEXT:
            index = static_cast<uint32_t>(statep.toInt32());

            if (index < typeRepr->asStruct()->fieldCount()) {
                idp.set(typeRepr->asStruct()->field(index).id);
                statep.setInt32(index + 1);
            } else {
                statep.setNull();
            }

            break;

          case JSENUMERATE_DESTROY:
            statep.setNull();
            break;
        }
        break;
    }

    return true;
}

/* static */ size_t
TypedContents::dataOffset()
{
    return JSObject::getPrivateDataOffset(JS_TYPEDOBJ_SLOTS + 1);
}

///////////////////////////////////////////////////////////////////////////
// Typed Objects

Class TypedObject::class_ = {
    "TypedObject",
    Class::NON_NATIVE |
    JSCLASS_HAS_RESERVED_SLOTS(JS_TYPEDOBJ_SLOTS) |
    JSCLASS_HAS_PRIVATE |
    JSCLASS_IMPLEMENTS_BARRIERS,
    JS_PropertyStub,
    JS_DeletePropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    TypedContents::obj_finalize,
    NULL,           /* checkAccess */
    NULL,           /* call        */
    NULL,           /* construct   */
    NULL,           /* hasInstance */
    TypedContents::obj_trace,
    JS_NULL_CLASS_EXT,
    {
        TypedContents::obj_lookupGeneric,
        TypedContents::obj_lookupProperty,
        TypedContents::obj_lookupElement,
        TypedContents::obj_lookupSpecial,
        TypedContents::obj_defineGeneric,
        TypedContents::obj_defineProperty,
        TypedContents::obj_defineElement,
        TypedContents::obj_defineSpecial,
        TypedContents::obj_getGeneric,
        TypedContents::obj_getProperty,
        TypedContents::obj_getElement,
        TypedContents::obj_getElementIfPresent,
        TypedContents::obj_getSpecial,
        TypedContents::obj_setGeneric,
        TypedContents::obj_setProperty,
        TypedContents::obj_setElement,
        TypedContents::obj_setSpecial,
        TypedContents::obj_getGenericAttributes,
        TypedContents::obj_setGenericAttributes,
        TypedContents::obj_deleteProperty,
        TypedContents::obj_deleteElement,
        TypedContents::obj_deleteSpecial,
        TypedContents::obj_enumerate,
        NULL, /* thisObject */
    }
};

/*static*/ JSObject *
TypedObject::createZeroed(JSContext *cx, HandleObject type)
{
    Rooted<TypedObject*> obj(cx, createUnattached<TypedObject>(cx, type));
    if (!obj)
        return NULL;

    TypeRepresentation *typeRepr = typeRepresentation(*type);
    size_t memsize = typeRepr->size();
    void *memory = JS_malloc(cx, memsize);
    if (!memory)
        return NULL;
    memset(memory, 0, memsize);
    obj->attach(memory);
    return obj;
}

/*static*/ bool
TypedObject::construct(JSContext *cx, unsigned int argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    RootedObject callee(cx, &args.callee());
    JS_ASSERT(IsTypeObject(*callee));

    RootedObject obj(cx, createZeroed(cx, callee));
    if (!obj)
        return false;

    if (argc == 1) {
        RootedValue initial(cx, args[0]);
        if (!ConvertAndCopyTo(cx, obj, initial))
            return false;
    }

    args.rval().setObject(*obj);
    return true;
}

///////////////////////////////////////////////////////////////////////////
// Handles

Class TypedHandle::class_ = {
    "Handle",
    Class::NON_NATIVE |
    JSCLASS_HAS_RESERVED_SLOTS(JS_TYPEDOBJ_SLOTS) |
    JSCLASS_HAS_PRIVATE |
    JSCLASS_IMPLEMENTS_BARRIERS,
    JS_PropertyStub,
    JS_DeletePropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    TypedContents::obj_finalize,
    NULL,           /* checkAccess */
    NULL,           /* call        */
    NULL,           /* construct   */
    NULL,           /* hasInstance */
    TypedContents::obj_trace,
    JS_NULL_CLASS_EXT,
    {
        TypedContents::obj_lookupGeneric,
        TypedContents::obj_lookupProperty,
        TypedContents::obj_lookupElement,
        TypedContents::obj_lookupSpecial,
        TypedContents::obj_defineGeneric,
        TypedContents::obj_defineProperty,
        TypedContents::obj_defineElement,
        TypedContents::obj_defineSpecial,
        TypedContents::obj_getGeneric,
        TypedContents::obj_getProperty,
        TypedContents::obj_getElement,
        TypedContents::obj_getElementIfPresent,
        TypedContents::obj_getSpecial,
        TypedContents::obj_setGeneric,
        TypedContents::obj_setProperty,
        TypedContents::obj_setElement,
        TypedContents::obj_setSpecial,
        TypedContents::obj_getGenericAttributes,
        TypedContents::obj_setGenericAttributes,
        TypedContents::obj_deleteProperty,
        TypedContents::obj_deleteElement,
        TypedContents::obj_deleteSpecial,
        TypedContents::obj_enumerate,
        NULL, /* thisObject */
    }
};

const JSFunctionSpec TypedHandle::handleStaticMethods[] = {
    {"move", {NULL, NULL}, 2, 0, "HandleMove"},
    {"get", {NULL, NULL}, 0, 0, "HandleGet"},
    {"set", {NULL, NULL}, 1, 0, "HandleSet"},
    {"isHandle", {NULL, NULL}, 1, 0, "HandleTest"},
    JS_FS_END
};

///////////////////////////////////////////////////////////////////////////
// Intrinsics

bool
js::NewTypedHandle(JSContext *cx, unsigned argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    JS_ASSERT(argc == 1);
    JS_ASSERT(args[0].isObject() && IsTypeObject(args[0].toObject()));

    RootedObject typeObj(cx, &args[0].toObject());
    Rooted<TypedHandle*> obj(
        cx, TypedContents::createUnattached<TypedHandle>(cx, typeObj));
    if (!obj)
        return false;
    args.rval().setObject(*obj);
    return true;
}

bool
js::NewDerivedTypedContents(JSContext *cx, unsigned argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    JS_ASSERT(argc == 3);
    JS_ASSERT(args[0].isObject() && IsTypeObject(args[0].toObject()));
    JS_ASSERT(args[1].isObject() && HasTypedContents(args[1].toObject()));
    JS_ASSERT(args[2].isInt32());

    RootedObject typeObj(cx, &args[0].toObject());
    RootedObject owner(cx, &args[1].toObject());
    int32_t offset = args[2].toInt32();

    Rooted<TypedContents*> obj(
        cx, TypedContents::createDerived(cx, typeObj, owner, offset));
    if (!obj)
        return false;

    args.rval().setObject(*obj);
    return true;
}

bool
js::AttachHandle(ThreadSafeContext *, unsigned argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    JS_ASSERT(argc == 3);
    JS_ASSERT(args[0].isObject() && args[0].toObject().is<TypedHandle>());
    JS_ASSERT(args[1].isObject() && HasTypedContents(args[1].toObject()));
    JS_ASSERT(args[2].isInt32());

    TypedHandle &handle = args[0].toObject().as<TypedHandle>();
    handle.attach(args[1].toObject(), args[2].toInt32());
    return true;
}

const JSJitInfo js::AttachHandleJitInfo =
    JS_JITINFO_NATIVE_PARALLEL(
        JSParallelNativeThreadSafeWrapper<js::AttachHandle>);

bool
js::ObjectIsTypeObject(ThreadSafeContext *, unsigned argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    JS_ASSERT(argc == 1);
    JS_ASSERT(args[0].isObject());
    args.rval().setBoolean(IsTypeObject(args[0].toObject()));
    return true;
}

const JSJitInfo js::ObjectIsTypeObjectJitInfo =
    JS_JITINFO_NATIVE_PARALLEL(
        JSParallelNativeThreadSafeWrapper<js::ObjectIsTypeObject>);

bool
js::ObjectIsTypeRepresentation(ThreadSafeContext *, unsigned argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    JS_ASSERT(argc == 1);
    JS_ASSERT(args[0].isObject());
    args.rval().setBoolean(TypeRepresentation::isOwnerObject(args[0].toObject()));
    return true;
}

const JSJitInfo js::ObjectIsTypeRepresentationJitInfo =
    JS_JITINFO_NATIVE_PARALLEL(
        JSParallelNativeThreadSafeWrapper<js::ObjectIsTypeRepresentation>);

bool
js::ObjectIsTypedHandle(ThreadSafeContext *, unsigned argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    JS_ASSERT(argc == 1);
    JS_ASSERT(args[0].isObject());
    args.rval().setBoolean(args[0].toObject().is<TypedHandle>());
    return true;
}

const JSJitInfo js::ObjectIsTypedHandleJitInfo =
    JS_JITINFO_NATIVE_PARALLEL(
        JSParallelNativeThreadSafeWrapper<js::ObjectIsTypedHandle>);

bool
js::ObjectIsTypedObject(ThreadSafeContext *, unsigned argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    JS_ASSERT(argc == 1);
    JS_ASSERT(args[0].isObject());
    args.rval().setBoolean(args[0].toObject().is<TypedObject>());
    return true;
}

const JSJitInfo js::ObjectIsTypedObjectJitInfo =
    JS_JITINFO_NATIVE_PARALLEL(
        JSParallelNativeThreadSafeWrapper<js::ObjectIsTypedObject>);

bool
js::IsAttached(ThreadSafeContext *cx, unsigned argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    JS_ASSERT(args[0].isObject() && HasTypedContents(args[0].toObject()));
    args.rval().setBoolean(TypedMem(args[0].toObject()) != NULL);
    return true;
}

const JSJitInfo js::IsAttachedJitInfo =
    JS_JITINFO_NATIVE_PARALLEL(
        JSParallelNativeThreadSafeWrapper<js::IsAttached>);

bool
js::ClampToUint8(ThreadSafeContext *, unsigned argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    JS_ASSERT(argc == 1);
    JS_ASSERT(args[0].isNumber());
    args.rval().setNumber(ClampDoubleToUint8(args[0].toNumber()));
    return true;
}

const JSJitInfo js::ClampToUint8JitInfo =
    JS_JITINFO_NATIVE_PARALLEL(
        JSParallelNativeThreadSafeWrapper<js::ClampToUint8>);

bool
js::Memcpy(ThreadSafeContext *, unsigned argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    JS_ASSERT(args.length() == 5);
    JS_ASSERT(args[0].isObject() && HasTypedContents(args[0].toObject()));
    JS_ASSERT(args[1].isInt32());
    JS_ASSERT(args[2].isObject() && HasTypedContents(args[2].toObject()));
    JS_ASSERT(args[3].isInt32());
    JS_ASSERT(args[4].isInt32());

    uint8_t *target = TypedMem(args[0].toObject()) + args[1].toInt32();
    uint8_t *source = TypedMem(args[2].toObject()) + args[3].toInt32();
    int32_t size = args[4].toInt32();
    memcpy(target, source, size);
    args.rval().setUndefined();
    return true;
}

const JSJitInfo js::MemcpyJitInfo =
    JS_JITINFO_NATIVE_PARALLEL(
        JSParallelNativeThreadSafeWrapper<js::Memcpy>);

template<typename T>
bool
js::StoreScalar<T>::Func(ThreadSafeContext *, unsigned argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    JS_ASSERT(args.length() == 3);
    JS_ASSERT(args[0].isObject() && HasTypedContents(args[0].toObject()));
    JS_ASSERT(args[1].isInt32());
    JS_ASSERT(args[2].isNumber());

    int32_t offset = args[1].toInt32();

    // Should be guaranteed by the typed objects API:
    JS_ASSERT(offset % alignof(T) == 0);

    T *target = (T*) (TypedMem(args[0].toObject()) + offset);
    double d = args[2].toNumber();
    *target = ConvertScalar<T>(d);
    args.rval().setUndefined();
    return true;
}

template<typename T>
const JSJitInfo
js::StoreScalar<T>::JitInfo =
    JS_JITINFO_NATIVE_PARALLEL(
        JSParallelNativeThreadSafeWrapper<Func>);

template<typename T>
bool
js::LoadScalar<T>::Func(ThreadSafeContext *, unsigned argc, Value *vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    JS_ASSERT(args.length() == 2);
    JS_ASSERT(args[0].isObject() && HasTypedContents(args[0].toObject()));
    JS_ASSERT(args[1].isInt32());

    int32_t offset = args[1].toInt32();

    // Should be guaranteed by the typed objects API:
    JS_ASSERT(offset % alignof(T) == 0);

    T *target = (T*) (TypedMem(args[0].toObject()) + offset);
    args.rval().setNumber((double) *target);
    return true;
}

template<typename T>
const JSJitInfo
js::LoadScalar<T>::JitInfo =
    JS_JITINFO_NATIVE_PARALLEL(
        JSParallelNativeThreadSafeWrapper<Func>);

#define PREDECLARED_LOAD_AND_STORE(_constant, _type, _name)                   \
    namespace js { template class StoreScalar<_type>;                         \
                   template class LoadScalar<_type>; }
JS_FOR_EACH_UNIQUE_SCALAR_TYPE_REPR_CTYPE(PREDECLARED_LOAD_AND_STORE)
