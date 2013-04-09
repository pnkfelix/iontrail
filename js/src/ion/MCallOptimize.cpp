/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=99:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jslibmath.h"
#include "jsmath.h"
#include "builtin/ParallelArray.h"
#include "builtin/TestingFunctions.h"

#include "MIR.h"
#include "MIRGraph.h"
#include "IonBuilder.h"

#include "vm/StringObject-inl.h"

namespace js {
namespace ion {

IonBuilder::InliningStatus
IonBuilder::inlineNativeCall(CallInfo &callInfo, JSNative native)
{
    // Array natives.
    if (native == js_Array)
        return inlineArray(callInfo);
    if (native == js::array_pop)
        return inlineArrayPopShift(callInfo, MArrayPopShift::Pop);
    if (native == js::array_shift)
        return inlineArrayPopShift(callInfo, MArrayPopShift::Shift);
    if (native == js::array_push)
        return inlineArrayPush(callInfo);
    if (native == js::array_concat)
        return inlineArrayConcat(callInfo);

    // Math natives.
    if (native == js_math_abs)
        return inlineMathAbs(callInfo);
    if (native == js_math_floor)
        return inlineMathFloor(callInfo);
    if (native == js_math_round)
        return inlineMathRound(callInfo);
    if (native == js_math_sqrt)
        return inlineMathSqrt(callInfo);
    if (native == js_math_max)
        return inlineMathMinMax(callInfo, true /* max */);
    if (native == js_math_min)
        return inlineMathMinMax(callInfo, false /* max */);
    if (native == js_math_pow)
        return inlineMathPow(callInfo);
    if (native == js_math_random)
        return inlineMathRandom(callInfo);
    if (native == js::math_imul)
        return inlineMathImul(callInfo);
    if (native == js::math_sin)
        return inlineMathFunction(callInfo, MMathFunction::Sin);
    if (native == js::math_cos)
        return inlineMathFunction(callInfo, MMathFunction::Cos);
    if (native == js::math_tan)
        return inlineMathFunction(callInfo, MMathFunction::Tan);
    if (native == js::math_log)
        return inlineMathFunction(callInfo, MMathFunction::Log);

    // String natives.
    if (native == js_String)
        return inlineStringObject(callInfo);
    if (native == js_str_charCodeAt)
        return inlineStrCharCodeAt(callInfo);
    if (native == js::str_fromCharCode)
        return inlineStrFromCharCode(callInfo);
    if (native == js_str_charAt)
        return inlineStrCharAt(callInfo);

    // RegExp natives.
    if (native == regexp_exec && !CallResultEscapes(pc))
        return inlineRegExpTest(callInfo);
    if (native == regexp_test)
        return inlineRegExpTest(callInfo);

    // Self-hosting
    if (native == intrinsic_ThrowError)
        return inlineThrowError(callInfo);
#ifdef DEBUG
    if (native == intrinsic_Dump)
        return inlineDump(callInfo);
#endif

    // Parallel Array
    if (native == intrinsic_UnsafeSetElement)
        return inlineUnsafeSetElement(callInfo);
    if (native == intrinsic_ShouldForceSequential)
        return inlineForceSequentialOrParallelJSActive(callInfo);
    if (native == testingFunc_parallelJSActive)
        return inlineForceSequentialOrParallelJSActive(callInfo);
    if (native == intrinsic_NewParallelArray)
        return inlineNewParallelArray(callInfo);
    if (native == ParallelArrayObject::construct)
        return inlineParallelArray(callInfo);
    if (native == intrinsic_NewDenseArray)
        return inlineNewDenseArray(callInfo);

    // Self-hosting
    if (native == intrinsic_ThrowError)
        return inlineThrowError(callInfo);
#ifdef DEBUG
    if (native == intrinsic_Dump)
        return inlineDump(callInfo);
#endif

    return InliningStatus_NotInlined;
}

types::StackTypeSet *
IonBuilder::getInlineReturnTypeSet()
{
    types::StackTypeSet *barrier;
    types::StackTypeSet *returnTypes = oracle->returnTypeSet(script(), pc, &barrier);
    JS_ASSERT(returnTypes);
    return returnTypes;
}

MIRType
IonBuilder::getInlineReturnType()
{
    types::StackTypeSet *returnTypes = getInlineReturnTypeSet();
    return MIRTypeFromValueType(returnTypes->getKnownTypeTag());
}

types::StackTypeSet *
IonBuilder::getInlineThisTypeSet(CallInfo &callInfo)
{
    types::StackTypeSet *thisTypes = oracle->getCallArg(script(), callInfo.argc(), 0, pc);
    JS_ASSERT(thisTypes);
    return thisTypes;
}

MIRType
IonBuilder::getInlineThisType(CallInfo &callInfo)
{
    types::StackTypeSet *argTypes = getInlineThisTypeSet(callInfo);
    return MIRTypeFromValueType(argTypes->getKnownTypeTag());
}

types::StackTypeSet *
IonBuilder::getInlineArgTypeSet(CallInfo &callInfo, uint32_t arg)
{
    types::StackTypeSet *argTypes = oracle->getCallArg(script(), callInfo.argc(), arg + 1, pc);
    JS_ASSERT(argTypes);
    return argTypes;
}

MIRType
IonBuilder::getInlineArgType(CallInfo &callInfo, uint32_t arg)
{
    types::StackTypeSet *argTypes = getInlineArgTypeSet(callInfo, arg);
    return MIRTypeFromValueType(argTypes->getKnownTypeTag());
}

IonBuilder::InliningStatus
IonBuilder::inlineMathFunction(CallInfo &callInfo, MMathFunction::Function function)
{
    if (callInfo.constructing())
        return InliningStatus_NotInlined;

    if (callInfo.argc() != 1)
        return InliningStatus_NotInlined;

    if (getInlineReturnType() != MIRType_Double)
        return InliningStatus_NotInlined;
    if (!IsNumberType(getInlineArgType(callInfo, 0)))
        return InliningStatus_NotInlined;

    callInfo.unwrapArgs();

    MathCache *cache = cx->runtime->getMathCache(cx);
    if (!cache)
        return InliningStatus_Error;

    MMathFunction *ins = MMathFunction::New(callInfo.getArg(0), function, cache);
    current->add(ins);
    current->push(ins);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineArray(CallInfo &callInfo)
{
    uint32_t initLength = 0;
    MNewArray::AllocatingBehaviour allocating = MNewArray::NewArray_Unallocating;

    // Multiple arguments imply array initialization, not just construction.
    if (callInfo.argc() >= 2) {
        initLength = callInfo.argc();
        allocating = MNewArray::NewArray_Allocating;
    }

    // A single integer argument denotes initial length.
    if (callInfo.argc() == 1) {
        if (getInlineArgType(callInfo, 0) != MIRType_Int32)
            return InliningStatus_NotInlined;
        MDefinition *arg = callInfo.getArg(0)->toPassArg()->getArgument();
        if (!arg->isConstant())
            return InliningStatus_NotInlined;

        // Negative lengths generate a RangeError, unhandled by the inline path.
        initLength = arg->toConstant()->value().toInt32();
        if (initLength >= JSObject::NELEMENTS_LIMIT)
            return InliningStatus_NotInlined;
    }

    callInfo.unwrapArgs();

    JSObject *templateObject = getNewArrayTemplateObject(initLength);
    if (!templateObject)
        return InliningStatus_Error;

    bool convertDoubles = oracle->arrayResultShouldHaveDoubleConversion(script(), pc);
    if (convertDoubles)
        templateObject->setShouldConvertDoubleElements();

    MNewArray *ins = new MNewArray(initLength, templateObject, allocating);
    current->add(ins);
    current->push(ins);

    if (callInfo.argc() >= 2) {
        // Get the elements vector.
        MElements *elements = MElements::New(ins);
        current->add(elements);

        // Store all values, no need to initialize the length after each as
        // jsop_initelem_array is doing because we do not expect to bailout
        // because the memory is supposed to be allocated by now.
        MConstant *id = NULL;
        for (uint32_t i = 0; i < initLength; i++) {
            id = MConstant::New(Int32Value(i));
            current->add(id);

            MDefinition *value = callInfo.getArg(i);
            if (convertDoubles) {
                MInstruction *valueDouble = MToDouble::New(value);
                current->add(valueDouble);
                value = valueDouble;
            }

            MStoreElement *store = MStoreElement::New(elements, id, value,
                                                      /* needsHoleCheck = */ false);
            current->add(store);
        }

        // Update the length.
        MSetInitializedLength *length = MSetInitializedLength::New(elements, id);
        current->add(length);

        if (!resumeAfter(length))
            return InliningStatus_Error;
    }

    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineArrayPopShift(CallInfo &callInfo, MArrayPopShift::Mode mode)
{
    if (callInfo.constructing())
        return InliningStatus_NotInlined;

    MIRType returnType = getInlineReturnType();
    if (returnType == MIRType_Undefined || returnType == MIRType_Null)
        return InliningStatus_NotInlined;
    if (getInlineThisType(callInfo) != MIRType_Object)
        return InliningStatus_NotInlined;

    // Pop and shift are only handled for dense arrays that have never been
    // used in an iterator: popping elements does not account for suppressing
    // deleted properties in active iterators.
    //
    // Inference's TypeConstraintCall generates the constraints that propagate
    // properties directly into the result type set.
    types::TypeObjectFlags unhandledFlags =
        types::OBJECT_FLAG_SPARSE_INDEXES |
        types::OBJECT_FLAG_LENGTH_OVERFLOW |
        types::OBJECT_FLAG_ITERATED;

    types::StackTypeSet *thisTypes = getInlineThisTypeSet(callInfo);
    if (thisTypes->getKnownClass() != &ArrayClass)
        return InliningStatus_NotInlined;
    if (thisTypes->hasObjectFlags(cx, unhandledFlags))
        return InliningStatus_NotInlined;
    RootedScript script(cx, script_);
    if (types::ArrayPrototypeHasIndexedProperty(cx, script))
        return InliningStatus_NotInlined;

    callInfo.unwrapArgs();

    types::StackTypeSet *returnTypes = getInlineReturnTypeSet();
    bool needsHoleCheck = thisTypes->hasObjectFlags(cx, types::OBJECT_FLAG_NON_PACKED);
    bool maybeUndefined = returnTypes->hasType(types::Type::UndefinedType());

    MArrayPopShift *ins = MArrayPopShift::New(callInfo.thisArg(), mode,
                                              needsHoleCheck, maybeUndefined);
    current->add(ins);
    current->push(ins);
    ins->setResultType(returnType);

    if (!resumeAfter(ins))
        return InliningStatus_Error;
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineArrayPush(CallInfo &callInfo)
{
    if (callInfo.argc() != 1 || callInfo.constructing())
        return InliningStatus_NotInlined;

    if (getInlineReturnType() != MIRType_Int32)
        return InliningStatus_NotInlined;
    if (getInlineThisType(callInfo) != MIRType_Object)
        return InliningStatus_NotInlined;

    // Inference's TypeConstraintCall generates the constraints that propagate
    // properties directly into the result type set.
    types::StackTypeSet *thisTypes = getInlineThisTypeSet(callInfo);
    if (thisTypes->getKnownClass() != &ArrayClass)
        return InliningStatus_NotInlined;
    if (thisTypes->hasObjectFlags(cx, types::OBJECT_FLAG_SPARSE_INDEXES |
                                  types::OBJECT_FLAG_LENGTH_OVERFLOW))
    {
        return InliningStatus_NotInlined;
    }
    RootedScript script(cx, script_);
    if (types::ArrayPrototypeHasIndexedProperty(cx, script))
        return InliningStatus_NotInlined;

    types::StackTypeSet::DoubleConversion conversion = thisTypes->convertDoubleElements(cx);
    if (conversion == types::StackTypeSet::AmbiguousDoubleConversion)
        return InliningStatus_NotInlined;

    callInfo.unwrapArgs();

    MDefinition *value = callInfo.getArg(0);
    if (conversion == types::StackTypeSet::AlwaysConvertToDoubles ||
        conversion == types::StackTypeSet::MaybeConvertToDoubles)
    {
        MInstruction *valueDouble = MToDouble::New(value);
        current->add(valueDouble);
        value = valueDouble;
    }

    MArrayPush *ins = MArrayPush::New(callInfo.thisArg(), value);
    current->add(ins);
    current->push(ins);

    if (!resumeAfter(ins))
        return InliningStatus_Error;
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineArrayConcat(CallInfo &callInfo)
{
    if (callInfo.argc() != 1 || callInfo.constructing())
        return InliningStatus_NotInlined;

    // Ensure |this|, argument and result are objects.
    if (getInlineReturnType() != MIRType_Object)
        return InliningStatus_NotInlined;
    if (getInlineThisType(callInfo) != MIRType_Object)
        return InliningStatus_NotInlined;
    if (getInlineArgType(callInfo, 0) != MIRType_Object)
        return InliningStatus_NotInlined;

    // |this| and the argument must be dense arrays.
    types::StackTypeSet *thisTypes = getInlineThisTypeSet(callInfo);
    types::StackTypeSet *argTypes = getInlineArgTypeSet(callInfo, 0);

    if (thisTypes->getKnownClass() != &ArrayClass)
        return InliningStatus_NotInlined;
    if (thisTypes->hasObjectFlags(cx, types::OBJECT_FLAG_SPARSE_INDEXES |
                                  types::OBJECT_FLAG_LENGTH_OVERFLOW))
    {
        return InliningStatus_NotInlined;
    }

    if (argTypes->getKnownClass() != &ArrayClass)
        return InliningStatus_NotInlined;
    if (argTypes->hasObjectFlags(cx, types::OBJECT_FLAG_SPARSE_INDEXES |
                                 types::OBJECT_FLAG_LENGTH_OVERFLOW))
    {
        return InliningStatus_NotInlined;
    }

    // Watch out for indexed properties on the prototype.
    RootedScript script(cx, script_);
    if (types::ArrayPrototypeHasIndexedProperty(cx, script))
        return InliningStatus_NotInlined;

    // Require the 'this' types to have a specific type matching the current
    // global, so we can create the result object inline.
    if (thisTypes->getObjectCount() != 1)
        return InliningStatus_NotInlined;

    types::TypeObject *thisType = thisTypes->getTypeObject(0);
    if (!thisType || &thisType->proto->global() != &script->global())
        return InliningStatus_NotInlined;

    // Constraints modeling this concat have not been generated by inference,
    // so check that type information already reflects possible side effects of
    // this call.
    types::HeapTypeSet *thisElemTypes = thisType->getProperty(cx, JSID_VOID, false);
    if (!thisElemTypes)
        return InliningStatus_Error;

    types::StackTypeSet *resTypes = getInlineReturnTypeSet();
    if (!resTypes->hasType(types::Type::ObjectType(thisType)))
        return InliningStatus_NotInlined;

    for (unsigned i = 0; i < argTypes->getObjectCount(); i++) {
        if (argTypes->getSingleObject(i))
            return InliningStatus_NotInlined;

        types::TypeObject *argType = argTypes->getTypeObject(i);
        if (!argType)
            continue;

        types::HeapTypeSet *elemTypes = argType->getProperty(cx, JSID_VOID, false);
        if (!elemTypes)
            return InliningStatus_Error;

        if (!elemTypes->knownSubset(cx, thisElemTypes))
            return InliningStatus_NotInlined;
    }

    // Inline the call.
    RootedObject templateObj(cx, NewDenseEmptyArray(cx, thisType->proto));
    if (!templateObj)
        return InliningStatus_Error;
    templateObj->setType(thisType);

    callInfo.unwrapArgs();

    MArrayConcat *ins = MArrayConcat::New(callInfo.thisArg(), callInfo.getArg(0), templateObj);
    current->add(ins);
    current->push(ins);

    if (!resumeAfter(ins))
        return InliningStatus_Error;
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineMathAbs(CallInfo &callInfo)
{
    if (callInfo.constructing())
        return InliningStatus_NotInlined;

    if (callInfo.argc() != 1)
        return InliningStatus_NotInlined;

    MIRType returnType = getInlineReturnType();
    MIRType argType = getInlineArgType(callInfo, 0);
    if (argType != MIRType_Int32 && argType != MIRType_Double)
        return InliningStatus_NotInlined;

    if (argType != returnType && returnType != MIRType_Int32)
        return InliningStatus_NotInlined;

    callInfo.unwrapArgs();

    MInstruction *ins = MAbs::New(callInfo.getArg(0), argType);
    current->add(ins);

    if (argType != returnType) {
        MToInt32 *toInt = MToInt32::New(ins);
        toInt->setCanBeNegativeZero(false);
        current->add(toInt);
        ins = toInt;
    }

    current->push(ins);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineMathFloor(CallInfo &callInfo)
{

    if (callInfo.constructing())
        return InliningStatus_NotInlined;

    if (callInfo.argc() != 1)
        return InliningStatus_NotInlined;

    MIRType argType = getInlineArgType(callInfo, 0);
    if (getInlineReturnType() != MIRType_Int32)
        return InliningStatus_NotInlined;

    // Math.floor(int(x)) == int(x)
    if (argType == MIRType_Int32) {
        callInfo.unwrapArgs();
        current->push(callInfo.getArg(0));
        return InliningStatus_Inlined;
    }

    if (argType == MIRType_Double) {
        callInfo.unwrapArgs();
        MFloor *ins = new MFloor(callInfo.getArg(0));
        current->add(ins);
        current->push(ins);
        return InliningStatus_Inlined;
    }

    return InliningStatus_NotInlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineMathRound(CallInfo &callInfo)
{
    if (callInfo.constructing())
        return InliningStatus_NotInlined;

    if (callInfo.argc() != 1)
        return InliningStatus_NotInlined;

    MIRType returnType = getInlineReturnType();
    MIRType argType = getInlineArgType(callInfo, 0);

    // Math.round(int(x)) == int(x)
    if (argType == MIRType_Int32 && returnType == MIRType_Int32) {
        callInfo.unwrapArgs();
        current->push(callInfo.getArg(0));
        return InliningStatus_Inlined;
    }

    if (argType == MIRType_Double && returnType == MIRType_Int32) {
        callInfo.unwrapArgs();
        MRound *ins = new MRound(callInfo.getArg(0));
        current->add(ins);
        current->push(ins);
        return InliningStatus_Inlined;
    }

    return InliningStatus_NotInlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineMathSqrt(CallInfo &callInfo)
{
    if (callInfo.constructing())
        return InliningStatus_NotInlined;

    if (callInfo.argc() != 1)
        return InliningStatus_NotInlined;

    MIRType argType = getInlineArgType(callInfo, 0);
    if (getInlineReturnType() != MIRType_Double)
        return InliningStatus_NotInlined;
    if (argType != MIRType_Double && argType != MIRType_Int32)
        return InliningStatus_NotInlined;

    callInfo.unwrapArgs();

    MSqrt *sqrt = MSqrt::New(callInfo.getArg(0));
    current->add(sqrt);
    current->push(sqrt);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineMathPow(CallInfo &callInfo)
{
    if (callInfo.constructing())
        return InliningStatus_NotInlined;

    if (callInfo.argc() != 2)
        return InliningStatus_NotInlined;

    // Typechecking.
    if (getInlineReturnType() != MIRType_Double)
        return InliningStatus_NotInlined;

    MIRType baseType = getInlineArgType(callInfo, 0);
    MIRType powerType = getInlineArgType(callInfo, 1);

    if (baseType != MIRType_Int32 && baseType != MIRType_Double)
        return InliningStatus_NotInlined;
    if (powerType != MIRType_Int32 && powerType != MIRType_Double)
        return InliningStatus_NotInlined;

    callInfo.unwrapArgs();

    MDefinition *base = callInfo.getArg(0);
    MDefinition *power = callInfo.getArg(1);

    // If the base is integer, convert it to a Double.
    // Safe since the output must be a Double.
    if (baseType == MIRType_Int32) {
        MToDouble *conv = MToDouble::New(base);
        current->add(conv);
        base = conv;
    }

    // Optimize some constant powers.
    if (callInfo.getArg(1)->isConstant()) {
        double pow;
        if (!ToNumber(GetIonContext()->cx, callInfo.getArg(1)->toConstant()->value(), &pow))
            return InliningStatus_Error;

        // Math.pow(x, 0.5) is a sqrt with edge-case detection.
        if (pow == 0.5) {
            MPowHalf *half = MPowHalf::New(base);
            current->add(half);
            current->push(half);
            return InliningStatus_Inlined;
        }

        // Math.pow(x, -0.5) == 1 / Math.pow(x, 0.5), even for edge cases.
        if (pow == -0.5) {
            MPowHalf *half = MPowHalf::New(base);
            current->add(half);
            MConstant *one = MConstant::New(DoubleValue(1.0));
            current->add(one);
            MDiv *div = MDiv::New(one, half, MIRType_Double);
            current->add(div);
            current->push(div);
            return InliningStatus_Inlined;
        }

        // Math.pow(x, 1) == x.
        if (pow == 1.0) {
            current->push(base);
            return InliningStatus_Inlined;
        }

        // Math.pow(x, 2) == x*x.
        if (pow == 2.0) {
            MMul *mul = MMul::New(base, base, MIRType_Double);
            current->add(mul);
            current->push(mul);
            return InliningStatus_Inlined;
        }

        // Math.pow(x, 3) == x*x*x.
        if (pow == 3.0) {
            MMul *mul1 = MMul::New(base, base, MIRType_Double);
            current->add(mul1);
            MMul *mul2 = MMul::New(base, mul1, MIRType_Double);
            current->add(mul2);
            current->push(mul2);
            return InliningStatus_Inlined;
        }

        // Math.pow(x, 4) == y*y, where y = x*x.
        if (pow == 4.0) {
            MMul *y = MMul::New(base, base, MIRType_Double);
            current->add(y);
            MMul *mul = MMul::New(y, y, MIRType_Double);
            current->add(mul);
            current->push(mul);
            return InliningStatus_Inlined;
        }
    }

    MPow *ins = MPow::New(base, power, powerType);
    current->add(ins);
    current->push(ins);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineMathRandom(CallInfo &callInfo)
{
    if (callInfo.constructing())
        return InliningStatus_NotInlined;

    if (getInlineReturnType() != MIRType_Double)
        return InliningStatus_NotInlined;

    callInfo.unwrapArgs();

    MRandom *rand = MRandom::New();
    current->add(rand);
    current->push(rand);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineMathImul(CallInfo &callInfo)
{
    if (callInfo.argc() != 2 || callInfo.constructing())
        return InliningStatus_NotInlined;

    MIRType returnType = getInlineReturnType();
    if (returnType != MIRType_Int32)
        return InliningStatus_NotInlined;

    if (!IsNumberType(getInlineArgType(callInfo, 0)))
        return InliningStatus_NotInlined;
    if (!IsNumberType(getInlineArgType(callInfo, 1)))
        return InliningStatus_NotInlined;

    callInfo.unwrapArgs();

    MInstruction *first = MTruncateToInt32::New(callInfo.getArg(0));
    current->add(first);

    MInstruction *second = MTruncateToInt32::New(callInfo.getArg(1));
    current->add(second);

    MMul *ins = MMul::New(first, second, MIRType_Int32, MMul::Integer);
    current->add(ins);
    current->push(ins);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineMathMinMax(CallInfo &callInfo, bool max)
{
    if (callInfo.argc() != 2 || callInfo.constructing())
        return InliningStatus_NotInlined;

    MIRType returnType = getInlineReturnType();
    if (!IsNumberType(returnType))
        return InliningStatus_NotInlined;

    MIRType arg0Type = getInlineArgType(callInfo, 0);
    if (!IsNumberType(arg0Type))
        return InliningStatus_NotInlined;
    MIRType arg1Type = getInlineArgType(callInfo, 1);
    if (!IsNumberType(arg1Type))
        return InliningStatus_NotInlined;

    if (returnType == MIRType_Int32 &&
        (arg0Type == MIRType_Double || arg1Type == MIRType_Double))
    {
        // We would need to inform TI, if we happen to return a double.
        return InliningStatus_NotInlined;
    }

    callInfo.unwrapArgs();

    MMinMax *ins = MMinMax::New(callInfo.getArg(0), callInfo.getArg(1), returnType, max);
    current->add(ins);
    current->push(ins);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineStringObject(CallInfo &callInfo)
{
    if (callInfo.argc() != 1 || !callInfo.constructing())
        return InliningStatus_NotInlined;

    // MToString only supports int32 or string values.
    MIRType type = getInlineArgType(callInfo, 0);
    if (type != MIRType_Int32 && type != MIRType_String)
        return InliningStatus_NotInlined;

    callInfo.unwrapArgs();

    RootedString emptyString(cx, cx->runtime->emptyString);
    RootedObject templateObj(cx, StringObject::create(cx, emptyString));
    if (!templateObj)
        return InliningStatus_Error;

    MNewStringObject *ins = MNewStringObject::New(callInfo.getArg(0), templateObj);
    current->add(ins);
    current->push(ins);

    if (!resumeAfter(ins))
        return InliningStatus_Error;

    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineStrCharCodeAt(CallInfo &callInfo)
{
    if (callInfo.argc() != 1 || callInfo.constructing())
        return InliningStatus_NotInlined;

    if (getInlineReturnType() != MIRType_Int32)
        return InliningStatus_NotInlined;
    if (getInlineThisType(callInfo) != MIRType_String)
        return InliningStatus_NotInlined;
    MIRType argType = getInlineArgType(callInfo, 0);
    if (argType != MIRType_Int32 && argType != MIRType_Double)
        return InliningStatus_NotInlined;

    callInfo.unwrapArgs();

    MInstruction *index = MToInt32::New(callInfo.getArg(0));
    current->add(index);

    MStringLength *length = MStringLength::New(callInfo.thisArg());
    current->add(length);

    index = addBoundsCheck(index, length);

    MCharCodeAt *charCode = MCharCodeAt::New(callInfo.thisArg(), index);
    current->add(charCode);
    current->push(charCode);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineStrFromCharCode(CallInfo &callInfo)
{
    if (callInfo.argc() != 1 || callInfo.constructing())
        return InliningStatus_NotInlined;

    if (getInlineReturnType() != MIRType_String)
        return InliningStatus_NotInlined;
    if (getInlineArgType(callInfo, 0) != MIRType_Int32)
        return InliningStatus_NotInlined;

    callInfo.unwrapArgs();

    MToInt32 *charCode = MToInt32::New(callInfo.getArg(0));
    current->add(charCode);

    MFromCharCode *string = MFromCharCode::New(charCode);
    current->add(string);
    current->push(string);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineStrCharAt(CallInfo &callInfo)
{
    if (callInfo.argc() != 1 || callInfo.constructing())
        return InliningStatus_NotInlined;

    if (getInlineReturnType() != MIRType_String)
        return InliningStatus_NotInlined;
    if (getInlineThisType(callInfo) != MIRType_String)
        return InliningStatus_NotInlined;
    MIRType argType = getInlineArgType(callInfo, 0);
    if (argType != MIRType_Int32 && argType != MIRType_Double)
        return InliningStatus_NotInlined;

    callInfo.unwrapArgs();

    MInstruction *index = MToInt32::New(callInfo.getArg(0));
    current->add(index);

    MStringLength *length = MStringLength::New(callInfo.thisArg());
    current->add(length);

    index = addBoundsCheck(index, length);

    // String.charAt(x) = String.fromCharCode(String.charCodeAt(x))
    MCharCodeAt *charCode = MCharCodeAt::New(callInfo.thisArg(), index);
    current->add(charCode);

    MFromCharCode *string = MFromCharCode::New(charCode);
    current->add(string);
    current->push(string);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineRegExpTest(CallInfo &callInfo)
{
    if (callInfo.argc() != 1 || callInfo.constructing())
        return InliningStatus_NotInlined;

    // TI can infer a NULL return type of regexp_test with eager compilation.
    if (CallResultEscapes(pc) && getInlineReturnType() != MIRType_Boolean)
        return InliningStatus_NotInlined;

    if (getInlineThisType(callInfo) != MIRType_Object)
        return InliningStatus_NotInlined;
    if (getInlineArgType(callInfo, 0) != MIRType_String)
        return InliningStatus_NotInlined;

    callInfo.unwrapArgs();

    MInstruction *match = MRegExpTest::New(callInfo.thisArg(), callInfo.getArg(0));
    current->add(match);
    current->push(match);
    if (!resumeAfter(match))
        return InliningStatus_Error;

    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineUnsafeSetElement(CallInfo &callInfo)
{
    uint32_t argc = callInfo.argc();
    if (argc < 3 || (argc % 3) != 0 || callInfo.constructing())
        return InliningStatus_NotInlined;

    /* Important:
     *
     * Here we inline each of the stores resulting from a call to
     * %UnsafeSetElement().  It is essential that these stores occur
     * atomically and cannot be interrupted by a stack or recursion
     * check.  If this is not true, race conditions can occur.
     */

    for (uint32_t base = 0; base < argc; base += 3) {
        uint32_t arri = base + 0;
        uint32_t idxi = base + 1;

        types::StackTypeSet *obj = getInlineArgTypeSet(callInfo, arri);
        types::StackTypeSet *id = getInlineArgTypeSet(callInfo, idxi);

        int arrayType;
        if (!oracle->elementWriteIsDenseNative(obj, id) &&
            !oracle->elementWriteIsTypedArray(obj, id, &arrayType))
        {
            return InliningStatus_NotInlined;
        }
    }

    callInfo.unwrapArgs();

    // Push the result first so that the stack depth matches up for
    // the potential bailouts that will occur in the stores below.
    MConstant *udef = MConstant::New(UndefinedValue());
    current->add(udef);
    current->push(udef);

    for (uint32_t base = 0; base < argc; base += 3) {
        uint32_t arri = base + 0;
        uint32_t idxi = base + 1;

        types::StackTypeSet *obj = getInlineArgTypeSet(callInfo, arri);
        types::StackTypeSet *id = getInlineArgTypeSet(callInfo, idxi);

        if (oracle->elementWriteIsDenseNative(obj, id)) {
            if (!inlineUnsafeSetDenseArrayElement(callInfo, base))
                return InliningStatus_Error;
            continue;
        }

        int arrayType;
        if (oracle->elementWriteIsTypedArray(obj, id, &arrayType)) {
            if (!inlineUnsafeSetTypedArrayElement(callInfo, base, arrayType))
                return InliningStatus_Error;
            continue;
        }

        JS_NOT_REACHED("Element access not dense array nor typed array");
    }

    return InliningStatus_Inlined;
}

bool
IonBuilder::inlineUnsafeSetDenseArrayElement(CallInfo &callInfo, uint32_t base)
{
    // Note: we do not check the conditions that are asserted as true
    // in intrinsic_UnsafeSetElement():
    // - arr is a dense array
    // - idx < initialized length
    // Furthermore, note that inference should be propagating
    // the type of the value to the JSID_VOID property of the array.

    uint32_t arri = base + 0;
    uint32_t idxi = base + 1;
    uint32_t elemi = base + 2;

    MElements *elements = MElements::New(callInfo.getArg(arri));
    current->add(elements);

    MToInt32 *id = MToInt32::New(callInfo.getArg(idxi));
    current->add(id);

    // We disable the hole check for this store.  This implies that if
    // there were setters on the prototype, they would not be invoked.
    // But this is actually the desired behavior.

    MStoreElement *store = MStoreElement::New(elements, id,
                                              callInfo.getArg(elemi),
                                              /* needsHoleCheck = */ false);
    store->setRacy();

    current->add(store);

    if (!resumeAfter(store))
        return false;

    return true;
}

bool
IonBuilder::inlineUnsafeSetTypedArrayElement(CallInfo &callInfo,
                                             uint32_t base,
                                             int arrayType)
{
    // Note: we do not check the conditions that are asserted as true
    // in intrinsic_UnsafeSetElement():
    // - arr is a typed array
    // - idx < length

    uint32_t arri = base + 1;
    uint32_t idxi = base + 2;
    uint32_t elemi = base + 3;

    MInstruction *elements = getTypedArrayElements(callInfo.getArg(arri));
    current->add(elements);

    MToInt32 *id = MToInt32::New(callInfo.getArg(idxi));
    current->add(id);

    MDefinition *value = callInfo.getArg(elemi);
    if (arrayType == TypedArray::TYPE_UINT8_CLAMPED) {
        value = MClampToUint8::New(value);
        current->add(value->toInstruction());
    }

    MStoreTypedArrayElement *store = MStoreTypedArrayElement::New(elements, id, value, arrayType);
    store->setRacy();

    current->add(store);

    if (!resumeAfter(store))
        return false;

    return true;
}

IonBuilder::InliningStatus
IonBuilder::inlineForceSequentialOrParallelJSActive(CallInfo &callInfo)
{
    if (callInfo.constructing())
        return InliningStatus_NotInlined;

    ExecutionMode executionMode = info().executionMode();
    switch (executionMode) {
      case SequentialExecution:
        // In sequential mode, leave as is, because we'd have to
        // access the "in warmup" flag of the runtime.
        return InliningStatus_NotInlined;

      case ParallelExecution:
        // During Parallel Exec, we always force sequential, so
        // replace with true.  This permits UCE to eliminate the
        // entire path as dead, which is important.
        callInfo.unwrapArgs();
        MConstant *ins = MConstant::New(BooleanValue(true));
        current->add(ins);
        current->push(ins);
        return InliningStatus_Inlined;
    }

    JS_NOT_REACHED("Invalid execution mode");
}

IonBuilder::InliningStatus
IonBuilder::inlineNewParallelArray(CallInfo &callInfo)
{
    // Rewrites a call like
    //
    //    NewParallelArray(ParallelArrayView, arg0, ..., argN)
    //
    // to
    //
    //    x = MNewParallelArray()
    //    ParallelArrayView(x, arg0, ..., argN)

    uint32_t argc = callInfo.argc();
    if (argc < 1 || callInfo.constructing())
        return InliningStatus_NotInlined;

    types::StackTypeSet *ctorTypes = getInlineArgTypeSet(callInfo, 0);
    RawObject targetObj = ctorTypes->getSingleton();
    RootedFunction target(cx);
    if (targetObj && targetObj->isFunction())
        target = targetObj->toFunction();
    if (target && target->hasScript() && target->nonLazyScript()->shouldCloneAtCallsite) {
        RootedScript scriptRoot(cx, script());
        target = CloneFunctionAtCallsite(cx, target, scriptRoot, pc);
        if (!target)
            return InliningStatus_Error;
    }
    MDefinition *ctor = makeCallsiteClone(
        target,
        callInfo.getArg(0)->toPassArg()->getArgument());

    // Discard the function.
    return inlineParallelArrayTail(callInfo, target, ctor,
                                   target ? NULL : ctorTypes, 1);
}

IonBuilder::InliningStatus
IonBuilder::inlineParallelArray(CallInfo &callInfo)
{
    if (!callInfo.constructing())
        return InliningStatus_NotInlined;

    uint32_t argc = callInfo.argc();
    RootedFunction target(cx, ParallelArrayObject::getConstructor(cx, argc));
    if (!target)
        return InliningStatus_Error;

    JS_ASSERT(target->nonLazyScript()->shouldCloneAtCallsite);
    RootedScript script(cx, script_);
    target = CloneFunctionAtCallsite(cx, target, script, pc);
    if (!target)
        return InliningStatus_Error;

    MConstant *ctor = MConstant::New(ObjectValue(*target));
    current->add(ctor);

    return inlineParallelArrayTail(callInfo, target, ctor, NULL, 0);
}

IonBuilder::InliningStatus
IonBuilder::inlineParallelArrayTail(CallInfo &callInfo,
                                    HandleFunction target,
                                    MDefinition *ctor,
                                    types::StackTypeSet *ctorTypes,
                                    uint32_t discards)
{
    // Rewrites either NewParallelArray(...) or new ParallelArray(...) from a
    // call to a native ctor into a call to the relevant function in the
    // self-hosted code.

    uint32_t argc = callInfo.argc() - discards;

    // Create the new parallel array object.  Parallel arrays have specially
    // constructed type objects, so we can only perform the inlining if we
    // already have one of these type objects.
    types::StackTypeSet *returnTypes = getInlineReturnTypeSet();
    if (returnTypes->getKnownTypeTag() != JSVAL_TYPE_OBJECT)
        return InliningStatus_NotInlined;
    if (returnTypes->getObjectCount() != 1)
        return InliningStatus_NotInlined;
    types::TypeObject *typeObject = returnTypes->getTypeObject(0);

    // Create the call and add in the non-this arguments.
    uint32_t targetArgs = argc;
    if (target && !target->isNative())
        targetArgs = Max<uint32_t>(target->nargs, argc);

    MCall *call = MCall::New(target, targetArgs + 1, argc, false, ctorTypes);
    if (!call)
        return InliningStatus_Error;

    callInfo.unwrapArgs();

    // Explicitly pad any missing arguments with |undefined|.
    // This permits skipping the argumentsRectifier.
    for (int32_t i = targetArgs; i > (int)argc; i--) {
        JS_ASSERT_IF(target, !target->isNative());
        MConstant *undef = MConstant::New(UndefinedValue());
        current->add(undef);
        MPassArg *pass = MPassArg::New(undef);
        current->add(pass);
        call->addArg(i, pass);
    }

    MPassArg *oldThis = MPassArg::New(callInfo.thisArg());
    current->add(oldThis);

    // Add explicit arguments.
    // Skip addArg(0) because it is reserved for this
    for (int32_t i = 0; i < argc; i++) {
        MDefinition *arg = callInfo.getArg(i + discards);
        MPassArg *passArg = MPassArg::New(arg);
        current->add(passArg);
        call->addArg(i + 1, passArg);
    }

    // Place an MPrepareCall before the first passed argument, before we
    // potentially perform rearrangement.
    MPrepareCall *start = new MPrepareCall;
    oldThis->block()->insertBefore(oldThis, start);
    call->initPrepareCall(start);

    // Create the MIR to allocate the new parallel array.  Take the type
    // object is taken from the prediction set.
    RootedObject templateObject(cx, ParallelArrayObject::newInstance(cx));
    if (!templateObject)
        return InliningStatus_Error;
    templateObject->setType(typeObject);
    MNewParallelArray *newObject = MNewParallelArray::New(templateObject);
    current->add(newObject);
    MPassArg *newThis = MPassArg::New(newObject);
    current->add(newThis);
    call->addArg(0, newThis);

    // Set the new callee.
    call->initFunction(ctor);

    current->add(call);
    current->push(newObject);

    if (!resumeAfter(call))
        return InliningStatus_Error;

    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineNewDenseArray(CallInfo &callInfo)
{
    if (callInfo.constructing() || callInfo.argc() != 1)
        return InliningStatus_NotInlined;

    // For now, in seq. mode we just call the C function.  In
    // par. mode we use inlined MIR.
    ExecutionMode executionMode = info().executionMode();
    switch (executionMode) {
      case SequentialExecution:
        return inlineNewDenseArrayForSequentialExecution(callInfo);
      case ParallelExecution:
        return inlineNewDenseArrayForParallelExecution(callInfo);
    }

    JS_NOT_REACHED("unknown ExecutionMode");
}

IonBuilder::InliningStatus
IonBuilder::inlineNewDenseArrayForSequentialExecution(CallInfo &callInfo)
{
    // not yet implemented; in seq. mode the C function is not so bad
    return InliningStatus_NotInlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineNewDenseArrayForParallelExecution(CallInfo &callInfo)
{
    // Create the new parallel array object.  Parallel arrays have specially
    // constructed type objects, so we can only perform the inlining if we
    // already have one of these type objects.
    types::StackTypeSet *returnTypes = getInlineReturnTypeSet();
    if (returnTypes->getKnownTypeTag() != JSVAL_TYPE_OBJECT)
        return InliningStatus_NotInlined;
    if (returnTypes->getObjectCount() != 1)
        return InliningStatus_NotInlined;
    types::TypeObject *typeObject = returnTypes->getTypeObject(0);

    RootedObject templateObject(cx, NewDenseAllocatedArray(cx, 0));
    if (!templateObject)
        return InliningStatus_Error;
    templateObject->setType(typeObject);

    callInfo.unwrapArgs();

    MParNewDenseArray *newObject = new MParNewDenseArray(graph().parSlice(),
                                                         callInfo.getArg(0),
                                                         templateObject);
    current->add(newObject);
    current->push(newObject);

    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineThrowError(CallInfo &callInfo)
{
    // In Parallel Execution, convert %ThrowError() into a bailout.

    if (callInfo.constructing())
        return InliningStatus_NotInlined;

    ExecutionMode executionMode = info().executionMode();
    switch (executionMode) {
      case SequentialExecution:
        return InliningStatus_NotInlined;
      case ParallelExecution:
        break;
    }

    callInfo.unwrapArgs();

    MParBailout *bailout = new MParBailout();
    if (!bailout)
        return InliningStatus_Error;
    current->end(bailout);

    current = newBlock(pc);
    if (!current)
        return InliningStatus_Error;

    MConstant *udef = MConstant::New(UndefinedValue());
    current->add(udef);
    current->push(udef);

    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineDump(CallInfo &callInfo)
{
    // In Parallel Execution, call ParDump.  We just need a debugging
    // aid!

    if (callInfo.constructing())
        return InliningStatus_NotInlined;

    ExecutionMode executionMode = info().executionMode();
    switch (executionMode) {
      case SequentialExecution:
        return InliningStatus_NotInlined;
      case ParallelExecution:
        break;
    }

    callInfo.unwrapArgs();

    MParDump *dump = new MParDump(callInfo.getArg(1));
    current->add(dump);

    MConstant *udef = MConstant::New(UndefinedValue());
    current->add(udef);
    current->push(udef);

    return InliningStatus_Inlined;
}

} // namespace ion
} // namespace js
