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
IonBuilder::inlineNativeCall(JSNative native, uint32_t argc, bool constructing)
{
    // Array natives.
    if (native == js_Array)
        return inlineArray(argc, constructing);
    if (native == js::array_pop)
        return inlineArrayPopShift(MArrayPopShift::Pop, argc, constructing);
    if (native == js::array_shift)
        return inlineArrayPopShift(MArrayPopShift::Shift, argc, constructing);
    if (native == js::array_push)
        return inlineArrayPush(argc, constructing);
    if (native == js::array_concat)
        return inlineArrayConcat(argc, constructing);

    // Math natives.
    if (native == js_math_abs)
        return inlineMathAbs(argc, constructing);
    if (native == js_math_floor)
        return inlineMathFloor(argc, constructing);
    if (native == js_math_round)
        return inlineMathRound(argc, constructing);
    if (native == js_math_sqrt)
        return inlineMathSqrt(argc, constructing);
    if (native == js_math_max)
        return inlineMathMinMax(true /* max */, argc, constructing);
    if (native == js_math_min)
        return inlineMathMinMax(false /* max */, argc, constructing);
    if (native == js_math_pow)
        return inlineMathPow(argc, constructing);
    if (native == js_math_random)
        return inlineMathRandom(argc, constructing);
    if (native == js::math_imul)
        return inlineMathImul(argc, constructing);
    if (native == js::math_sin)
        return inlineMathFunction(MMathFunction::Sin, argc, constructing);
    if (native == js::math_cos)
        return inlineMathFunction(MMathFunction::Cos, argc, constructing);
    if (native == js::math_tan)
        return inlineMathFunction(MMathFunction::Tan, argc, constructing);
    if (native == js::math_log)
        return inlineMathFunction(MMathFunction::Log, argc, constructing);

    // String natives.
    if (native == js_String)
        return inlineStringObject(argc, constructing);
    if (native == js_str_charCodeAt)
        return inlineStrCharCodeAt(argc, constructing);
    if (native == js::str_fromCharCode)
        return inlineStrFromCharCode(argc, constructing);
    if (native == js_str_charAt)
        return inlineStrCharAt(argc, constructing);

    // RegExp natives.
    if (native == regexp_exec && !CallResultEscapes(pc))
        return inlineRegExpTest(argc, constructing);
    if (native == regexp_test)
        return inlineRegExpTest(argc, constructing);

    // Parallel Array
    if (native == intrinsic_UnsafeSetElement)
        return inlineUnsafeSetElement(argc, constructing);
    if (native == intrinsic_ForceSequential)
        return inlineForceSequentialOrInParallelSection(argc, constructing);
    if (native == testingFunc_inParallelSection)
        return inlineForceSequentialOrInParallelSection(argc, constructing);
    if (native == intrinsic_NewParallelArray)
        return inlineNewParallelArray(argc, constructing);
    if (native == ParallelArrayObject::construct)
        return inlineParallelArray(argc, constructing);
    if (native == intrinsic_DenseArray)
        return inlineDenseArray(argc, constructing);

    // Self-hosting
    if (native == intrinsic_ThrowError)
        return inlineThrowError(argc, constructing);
#ifdef DEBUG
    if (native == intrinsic_Dump)
        return inlineDump(argc, constructing);
#endif

    return InliningStatus_NotInlined;
}

static MDefinition *
UnwrapAndDiscardPassArg(MPassArg *passArg)
{
    MBasicBlock *block = passArg->block();
    MDefinition *wrapped = passArg->getArgument();
    passArg->replaceAllUsesWith(wrapped);
    block->discard(passArg);
    return wrapped;
}

bool
IonBuilder::discardCallArgs(uint32_t argc, MDefinitionVector &argv, MBasicBlock *bb)
{
    if (!argv.resizeUninitialized(argc + 1))
        return false;

    for (int32_t i = argc; i >= 0; i--)
        argv[i] = UnwrapAndDiscardPassArg(bb->pop()->toPassArg());

    return true;
}

bool
IonBuilder::discardCall(uint32_t argc, MDefinitionVector &argv, MBasicBlock *bb)
{
    if (!discardCallArgs(argc, argv, bb))
        return false;

    // Function MDefinition implicitly consumed by inlining.
    bb->pop();
    return true;
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
IonBuilder::getInlineArgTypeSet(uint32_t argc, uint32_t arg)
{
    types::StackTypeSet *argTypes = oracle->getCallArg(script(), argc, arg, pc);
    JS_ASSERT(argTypes);
    return argTypes;
}

MIRType
IonBuilder::getInlineArgType(uint32_t argc, uint32_t arg)
{
    types::StackTypeSet *argTypes = getInlineArgTypeSet(argc, arg);
    return MIRTypeFromValueType(argTypes->getKnownTypeTag());
}

IonBuilder::InliningStatus
IonBuilder::inlineMathFunction(MMathFunction::Function function, uint32_t argc, bool constructing)
{
    if (constructing)
        return InliningStatus_NotInlined;

    if (argc != 1)
        return InliningStatus_NotInlined;

    if (getInlineReturnType() != MIRType_Double)
        return InliningStatus_NotInlined;
    if (!IsNumberType(getInlineArgType(argc, 1)))
        return InliningStatus_NotInlined;

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

    MathCache *cache = cx->runtime->getMathCache(cx);
    if (!cache)
        return InliningStatus_Error;

    MMathFunction *ins = MMathFunction::New(argv[1], function, cache);
    current->add(ins);
    current->push(ins);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineArray(uint32_t argc, bool constructing)
{
    uint32_t initLength = 0;
    MNewArray::AllocatingBehaviour allocating = MNewArray::NewArray_Unallocating;

    // Multiple arguments imply array initialization, not just construction.
    if (argc >= 2) {
        initLength = argc;
        allocating = MNewArray::NewArray_Allocating;
    }

    // A single integer argument denotes initial length.
    if (argc == 1) {
        if (getInlineArgType(argc, 1) != MIRType_Int32)
            return InliningStatus_NotInlined;
        MDefinition *arg = current->peek(-1)->toPassArg()->getArgument();
        if (!arg->isConstant())
            return InliningStatus_NotInlined;

        // Negative lengths generate a RangeError, unhandled by the inline path.
        initLength = arg->toConstant()->value().toInt32();
        if (initLength >= JSObject::NELEMENTS_LIMIT)
            return InliningStatus_NotInlined;
    }

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

    JSObject *templateObject = getNewArrayTemplateObject(initLength);
    if (!templateObject)
        return InliningStatus_Error;

    MNewArray *ins = new MNewArray(initLength, templateObject, allocating);
    current->add(ins);
    current->push(ins);

    if (argc >= 2) {
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

            MStoreElement *store = MStoreElement::New(elements, id, argv[i + 1]);
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
IonBuilder::inlineArrayPopShift(MArrayPopShift::Mode mode, uint32_t argc, bool constructing)
{
    if (constructing)
        return InliningStatus_NotInlined;

    MIRType returnType = getInlineReturnType();
    if (returnType == MIRType_Undefined || returnType == MIRType_Null)
        return InliningStatus_NotInlined;
    if (getInlineArgType(argc, 0) != MIRType_Object)
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

    types::StackTypeSet *thisTypes = getInlineArgTypeSet(argc, 0);
    if (thisTypes->getKnownClass() != &ArrayClass)
        return InliningStatus_NotInlined;
    if (thisTypes->hasObjectFlags(cx, unhandledFlags))
        return InliningStatus_NotInlined;
    RootedScript script(cx, script_);
    if (types::ArrayPrototypeHasIndexedProperty(cx, script))
        return InliningStatus_NotInlined;

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

    types::StackTypeSet *returnTypes = getInlineReturnTypeSet();
    bool needsHoleCheck = thisTypes->hasObjectFlags(cx, types::OBJECT_FLAG_NON_PACKED);
    bool maybeUndefined = returnTypes->hasType(types::Type::UndefinedType());

    MArrayPopShift *ins = MArrayPopShift::New(argv[0], mode, needsHoleCheck, maybeUndefined);
    current->add(ins);
    current->push(ins);
    ins->setResultType(returnType);

    if (!resumeAfter(ins))
        return InliningStatus_Error;
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineArrayPush(uint32_t argc, bool constructing)
{
    if (argc != 1 || constructing)
        return InliningStatus_NotInlined;

    if (getInlineReturnType() != MIRType_Int32)
        return InliningStatus_NotInlined;
    if (getInlineArgType(argc, 0) != MIRType_Object)
        return InliningStatus_NotInlined;

    // Inference's TypeConstraintCall generates the constraints that propagate
    // properties directly into the result type set.
    types::StackTypeSet *thisTypes = getInlineArgTypeSet(argc, 0);
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

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

    MArrayPush *ins = MArrayPush::New(argv[0], argv[1]);
    current->add(ins);
    current->push(ins);

    if (!resumeAfter(ins))
        return InliningStatus_Error;
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineArrayConcat(uint32_t argc, bool constructing)
{
    if (argc != 1 || constructing)
        return InliningStatus_NotInlined;

    // Ensure |this|, argument and result are objects.
    if (getInlineReturnType() != MIRType_Object)
        return InliningStatus_NotInlined;
    if (getInlineArgType(argc, 0) != MIRType_Object)
        return InliningStatus_NotInlined;
    if (getInlineArgType(argc, 1) != MIRType_Object)
        return InliningStatus_NotInlined;

    // |this| and the argument must be dense arrays.
    types::StackTypeSet *thisTypes = getInlineArgTypeSet(argc, 0);
    types::StackTypeSet *argTypes = getInlineArgTypeSet(argc, 1);

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

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

    MArrayConcat *ins = MArrayConcat::New(argv[0], argv[1], templateObj);
    current->add(ins);
    current->push(ins);

    if (!resumeAfter(ins))
        return InliningStatus_Error;
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineMathAbs(uint32_t argc, bool constructing)
{
    if (constructing)
        return InliningStatus_NotInlined;

    if (argc != 1)
        return InliningStatus_NotInlined;

    MIRType returnType = getInlineReturnType();
    MIRType argType = getInlineArgType(argc, 1);
    if (argType != MIRType_Int32 && argType != MIRType_Double)
        return InliningStatus_NotInlined;
    if (argType != returnType)
        return InliningStatus_NotInlined;

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

    MAbs *ins = MAbs::New(argv[1], returnType);
    current->add(ins);
    current->push(ins);

    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineMathFloor(uint32_t argc, bool constructing)
{

    if (constructing)
        return InliningStatus_NotInlined;

    if (argc != 1)
        return InliningStatus_NotInlined;

    MIRType argType = getInlineArgType(argc, 1);
    if (getInlineReturnType() != MIRType_Int32)
        return InliningStatus_NotInlined;

    // Math.floor(int(x)) == int(x)
    if (argType == MIRType_Int32) {
        MDefinitionVector argv;
        if (!discardCall(argc, argv, current))
            return InliningStatus_Error;
        current->push(argv[1]);
        return InliningStatus_Inlined;
    }

    if (argType == MIRType_Double) {
        MDefinitionVector argv;
        if (!discardCall(argc, argv, current))
            return InliningStatus_Error;
        MFloor *ins = new MFloor(argv[1]);
        current->add(ins);
        current->push(ins);
        return InliningStatus_Inlined;
    }

    return InliningStatus_NotInlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineMathRound(uint32_t argc, bool constructing)
{
    if (constructing)
        return InliningStatus_NotInlined;

    if (argc != 1)
        return InliningStatus_NotInlined;

    MIRType returnType = getInlineReturnType();
    MIRType argType = getInlineArgType(argc, 1);

    // Math.round(int(x)) == int(x)
    if (argType == MIRType_Int32 && returnType == MIRType_Int32) {
        MDefinitionVector argv;
        if (!discardCall(argc, argv, current))
            return InliningStatus_Error;
        current->push(argv[1]);
        return InliningStatus_Inlined;
    }

    if (argType == MIRType_Double && returnType == MIRType_Int32) {
        MDefinitionVector argv;
        if (!discardCall(argc, argv, current))
            return InliningStatus_Error;
        MRound *ins = new MRound(argv[1]);
        current->add(ins);
        current->push(ins);
        return InliningStatus_Inlined;
    }

    return InliningStatus_NotInlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineMathSqrt(uint32_t argc, bool constructing)
{
    if (constructing)
        return InliningStatus_NotInlined;

    if (argc != 1)
        return InliningStatus_NotInlined;

    MIRType argType = getInlineArgType(argc, 1);
    if (getInlineReturnType() != MIRType_Double)
        return InliningStatus_NotInlined;
    if (argType != MIRType_Double && argType != MIRType_Int32)
        return InliningStatus_NotInlined;

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

    MSqrt *sqrt = MSqrt::New(argv[1]);
    current->add(sqrt);
    current->push(sqrt);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineMathPow(uint32_t argc, bool constructing)
{
    if (constructing)
        return InliningStatus_NotInlined;

    if (argc != 2)
        return InliningStatus_NotInlined;

    // Typechecking.
    if (getInlineReturnType() != MIRType_Double)
        return InliningStatus_NotInlined;

    MIRType arg1Type = getInlineArgType(argc, 1);
    MIRType arg2Type = getInlineArgType(argc, 2);

    if (arg1Type != MIRType_Int32 && arg1Type != MIRType_Double)
        return InliningStatus_NotInlined;
    if (arg2Type != MIRType_Int32 && arg2Type != MIRType_Double)
        return InliningStatus_NotInlined;

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

    // If the non-power input is integer, convert it to a Double.
    // Safe since the output must be a Double.
    if (arg1Type == MIRType_Int32) {
        MToDouble *conv = MToDouble::New(argv[1]);
        current->add(conv);
        argv[1] = conv;
    }

    // Optimize some constant powers.
    if (argv[2]->isConstant()) {
        double pow;
        if (!ToNumber(GetIonContext()->cx, argv[2]->toConstant()->value(), &pow))
            return InliningStatus_Error;

        // Math.pow(x, 0.5) is a sqrt with edge-case detection.
        if (pow == 0.5) {
            MPowHalf *half = MPowHalf::New(argv[1]);
            current->add(half);
            current->push(half);
            return InliningStatus_Inlined;
        }

        // Math.pow(x, -0.5) == 1 / Math.pow(x, 0.5), even for edge cases.
        if (pow == -0.5) {
            MPowHalf *half = MPowHalf::New(argv[1]);
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
            current->push(argv[1]);
            return InliningStatus_Inlined;
        }

        // Math.pow(x, 2) == x*x.
        if (pow == 2.0) {
            MMul *mul = MMul::New(argv[1], argv[1], MIRType_Double);
            current->add(mul);
            current->push(mul);
            return InliningStatus_Inlined;
        }

        // Math.pow(x, 3) == x*x*x.
        if (pow == 3.0) {
            MMul *mul1 = MMul::New(argv[1], argv[1], MIRType_Double);
            current->add(mul1);
            MMul *mul2 = MMul::New(argv[1], mul1, MIRType_Double);
            current->add(mul2);
            current->push(mul2);
            return InliningStatus_Inlined;
        }

        // Math.pow(x, 4) == y*y, where y = x*x.
        if (pow == 4.0) {
            MMul *y = MMul::New(argv[1], argv[1], MIRType_Double);
            current->add(y);
            MMul *mul = MMul::New(y, y, MIRType_Double);
            current->add(mul);
            current->push(mul);
            return InliningStatus_Inlined;
        }
    }

    MPow *ins = MPow::New(argv[1], argv[2], arg2Type);
    current->add(ins);
    current->push(ins);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineMathRandom(uint32_t argc, bool constructing)
{
    if (constructing)
        return InliningStatus_NotInlined;

    if (getInlineReturnType() != MIRType_Double)
        return InliningStatus_NotInlined;

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

    MRandom *rand = MRandom::New();
    current->add(rand);
    current->push(rand);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineMathImul(uint32_t argc, bool constructing)
{
    if (argc != 2 || constructing)
        return InliningStatus_NotInlined;

    MIRType returnType = getInlineReturnType();
    if (returnType != MIRType_Int32)
        return InliningStatus_NotInlined;

    if (!IsNumberType(getInlineArgType(argc, 1)))
        return InliningStatus_NotInlined;
    if (!IsNumberType(getInlineArgType(argc, 2)))
        return InliningStatus_NotInlined;

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

    MInstruction *first = MTruncateToInt32::New(argv[1]);
    current->add(first);

    MInstruction *second = MTruncateToInt32::New(argv[2]);
    current->add(second);

    MMul *ins = MMul::New(first, second, MIRType_Int32, MMul::Integer);
    current->add(ins);
    current->push(ins);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineMathMinMax(bool max, uint32_t argc, bool constructing)
{
    if (argc != 2 || constructing)
        return InliningStatus_NotInlined;

    MIRType returnType = getInlineReturnType();
    if (!IsNumberType(returnType))
        return InliningStatus_NotInlined;

    MIRType arg1Type = getInlineArgType(argc, 1);
    if (!IsNumberType(arg1Type))
        return InliningStatus_NotInlined;
    MIRType arg2Type = getInlineArgType(argc, 2);
    if (!IsNumberType(arg2Type))
        return InliningStatus_NotInlined;

    if (returnType == MIRType_Int32 &&
        (arg1Type == MIRType_Double || arg2Type == MIRType_Double))
    {
        // We would need to inform TI, if we happen to return a double.
        return InliningStatus_NotInlined;
    }

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

    MMinMax *ins = MMinMax::New(argv[1], argv[2], returnType, max);
    current->add(ins);
    current->push(ins);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineStringObject(uint32_t argc, bool constructing)
{
    if (argc != 1 || !constructing)
        return InliningStatus_NotInlined;

    // MToString only supports int32 or string values.
    MIRType type = getInlineArgType(argc, 1);
    if (type != MIRType_Int32 && type != MIRType_String)
        return InliningStatus_NotInlined;

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

    RootedString emptyString(cx, cx->runtime->emptyString);
    RootedObject templateObj(cx, StringObject::create(cx, emptyString));
    if (!templateObj)
        return InliningStatus_Error;

    MNewStringObject *ins = MNewStringObject::New(argv[1], templateObj);
    current->add(ins);
    current->push(ins);

    if (!resumeAfter(ins))
        return InliningStatus_Error;

    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineStrCharCodeAt(uint32_t argc, bool constructing)
{
    if (argc != 1 || constructing)
        return InliningStatus_NotInlined;

    if (getInlineReturnType() != MIRType_Int32)
        return InliningStatus_NotInlined;
    if (getInlineArgType(argc, 0) != MIRType_String)
        return InliningStatus_NotInlined;
    MIRType argType = getInlineArgType(argc, 1);
    if (argType != MIRType_Int32 && argType != MIRType_Double)
        return InliningStatus_NotInlined;

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

    MInstruction *index = MToInt32::New(argv[1]);
    current->add(index);

    MStringLength *length = MStringLength::New(argv[0]);
    current->add(length);

    index = addBoundsCheck(index, length);

    MCharCodeAt *charCode = MCharCodeAt::New(argv[0], index);
    current->add(charCode);
    current->push(charCode);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineStrFromCharCode(uint32_t argc, bool constructing)
{
    if (argc != 1 || constructing)
        return InliningStatus_NotInlined;

    if (getInlineReturnType() != MIRType_String)
        return InliningStatus_NotInlined;
    if (getInlineArgType(argc, 1) != MIRType_Int32)
        return InliningStatus_NotInlined;

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

    MToInt32 *charCode = MToInt32::New(argv[1]);
    current->add(charCode);

    MFromCharCode *string = MFromCharCode::New(charCode);
    current->add(string);
    current->push(string);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineStrCharAt(uint32_t argc, bool constructing)
{
    if (argc != 1 || constructing)
        return InliningStatus_NotInlined;

    if (getInlineReturnType() != MIRType_String)
        return InliningStatus_NotInlined;
    if (getInlineArgType(argc, 0) != MIRType_String)
        return InliningStatus_NotInlined;
    MIRType argType = getInlineArgType(argc, 1);
    if (argType != MIRType_Int32 && argType != MIRType_Double)
        return InliningStatus_NotInlined;

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

    MInstruction *index = MToInt32::New(argv[1]);
    current->add(index);

    MStringLength *length = MStringLength::New(argv[0]);
    current->add(length);

    index = addBoundsCheck(index, length);

    // String.charAt(x) = String.fromCharCode(String.charCodeAt(x))
    MCharCodeAt *charCode = MCharCodeAt::New(argv[0], index);
    current->add(charCode);

    MFromCharCode *string = MFromCharCode::New(charCode);
    current->add(string);
    current->push(string);
    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineRegExpTest(uint32_t argc, bool constructing)
{
    if (argc != 1 || constructing)
        return InliningStatus_NotInlined;

    // TI can infer a NULL return type of regexp_test with eager compilation.
    if (CallResultEscapes(pc) && getInlineReturnType() != MIRType_Boolean)
        return InliningStatus_NotInlined;

    if (getInlineArgType(argc, 0) != MIRType_Object)
        return InliningStatus_NotInlined;
    if (getInlineArgType(argc, 1) != MIRType_String)
        return InliningStatus_NotInlined;

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

    MInstruction *match = MRegExpTest::New(argv[0], argv[1]);
    current->add(match);
    current->push(match);
    if (!resumeAfter(match))
        return InliningStatus_Error;

    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineUnsafeSetElement(uint32_t argc, bool constructing)
{
    if (argc < 3 || (argc % 3) != 0 || constructing)
        return InliningStatus_NotInlined;

    /* Important:
     *
     * Here we inline each of the stores resulting from a call to
     * %UnsafeSetElement().  It is essential that these stores occur
     * atomically and cannot be interrupted by a stack or recursion
     * check.  If this is not true, race conditions can occur.
     */

    for (uint32_t base = 0; base < argc; base += 3) {
        uint32_t arri = base + 1;
        uint32_t idxi = base + 2;

        types::StackTypeSet *obj = getInlineArgTypeSet(argc, arri);
        types::StackTypeSet *id = getInlineArgTypeSet(argc, idxi);

        int arrayType;
        if (!oracle->elementAccessIsDenseArray(obj, id) &&
            !oracle->elementAccessIsTypedArray(obj, id, &arrayType))
            return InliningStatus_NotInlined;
    }

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

    // Push the result first so that the stack depth matches up for
    // the potential bailouts that will occur in the stores below.
    MConstant *udef = MConstant::New(UndefinedValue());
    current->add(udef);
    current->push(udef);

    for (uint32_t base = 0; base < argc; base += 3) {
        uint32_t arri = base + 1;
        uint32_t idxi = base + 2;

        types::StackTypeSet *obj = getInlineArgTypeSet(argc, arri);
        types::StackTypeSet *id = getInlineArgTypeSet(argc, idxi);

        if (oracle->elementAccessIsDenseArray(obj, id)) {
            if (!inlineUnsafeSetDenseArrayElement(argc, argv, base))
                return InliningStatus_Error;
            continue;
        }

        int arrayType;
        if (oracle->elementAccessIsTypedArray(obj, id, &arrayType)) {
            if (!inlineUnsafeSetTypedArrayElement(argc, argv, base, arrayType))
                return InliningStatus_Error;
            continue;
        }

        JS_NOT_REACHED("Element access not dense array nor typed array");
    }

    return InliningStatus_Inlined;
}

bool
IonBuilder::inlineUnsafeSetDenseArrayElement(uint32_t argc, MDefinitionVector &argv, uint32_t base)
{
    // Note: we do not check the conditions that are asserted as true
    // in intrinsic_UnsafeSetElement():
    // - arr is a dense array
    // - idx < initialized length
    // Furthermore, note that inference should be propagating
    // the type of the value to the JSID_VOID property of the array.

    uint32_t arri = base + 1;
    uint32_t idxi = base + 2;
    uint32_t elemi = base + 3;

    MElements *elements = MElements::New(argv[arri]);
    current->add(elements);

    MToInt32 *id = MToInt32::New(argv[idxi]);
    current->add(id);

    MStoreElement *store = MStoreElement::New(elements, id, argv[elemi]);
    store->setRacy();

    current->add(store);

    if (!resumeAfter(store))
        return false;

    return true;
}

bool
IonBuilder::inlineUnsafeSetTypedArrayElement(uint32_t argc, MDefinitionVector &argv,
                                             uint32_t base, int arrayType)
{
    // Note: we do not check the conditions that are asserted as true
    // in intrinsic_UnsafeSetElement():
    // - arr is a typed array
    // - idx < length

    uint32_t arri = base + 1;
    uint32_t idxi = base + 2;
    uint32_t elemi = base + 3;

    MInstruction *elements = getTypedArrayElements(argv[arri]);
    current->add(elements);

    MToInt32 *id = MToInt32::New(argv[idxi]);
    current->add(id);

    MDefinition *value = argv[elemi];
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
IonBuilder::inlineForceSequentialOrInParallelSection(uint32_t argc, bool constructing)
{
    if (constructing)
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
        MDefinitionVector argv;
        if (!discardCall(argc, argv, current))
            return InliningStatus_Error;
        MConstant *ins = MConstant::New(BooleanValue(true));
        current->add(ins);
        current->push(ins);
        return InliningStatus_Inlined;
    }

    JS_NOT_REACHED("Invalid execution mode");
}

IonBuilder::InliningStatus
IonBuilder::inlineNewParallelArray(uint32_t argc, bool constructing)
{
    if (argc < 1 || constructing)
        return InliningStatus_NotInlined;

    types::StackTypeSet *ctorTypes = getInlineArgTypeSet(argc, 1);
    RawObject targetObj = ctorTypes->getSingleton();
    RootedFunction target(cx);
    if (targetObj && targetObj->isFunction())
        target = targetObj->toFunction();
    if (target && target->isCloneAtCallsite()) {
        RootedScript scriptRoot(cx, script());
        target = CloneFunctionAtCallsite(cx, target, scriptRoot, pc);
        if (!target)
            return InliningStatus_Error;
    }
    MDefinition *ctor = makeCallsiteClone(target,
                                          current->peek(-(argc + 1))->toPassArg()->getArgument());

    // Discard the function.
    return inlineParallelArrayTail(argc, target, ctor, target ? NULL : ctorTypes, 1);
}

IonBuilder::InliningStatus
IonBuilder::inlineParallelArray(uint32_t argc, bool constructing)
{
    if (!constructing)
        return InliningStatus_NotInlined;

    RootedFunction target(cx, ParallelArrayObject::getConstructor(cx, argc));
    if (!target)
        return InliningStatus_Error;

    JS_ASSERT(target->isCloneAtCallsite());
    RootedScript script(cx, script_);
    target = CloneFunctionAtCallsite(cx, target, script, pc);
    if (!target)
        return InliningStatus_Error;

    MConstant *ctor = MConstant::New(ObjectValue(*target));
    current->add(ctor);

    return inlineParallelArrayTail(argc, target, ctor, NULL, 0);
}

IonBuilder::InliningStatus
IonBuilder::inlineParallelArrayTail(uint32_t argc, HandleFunction target, MDefinition *ctor,
                                    types::StackTypeSet *ctorTypes, int32_t discards)
{
    // Rewrites either %NewParallelArray(...) or new ParallelArray(...) from a
    // call to a native ctor into a call to the relevant function in the
    // self-hosted code.

    // Create the new parallel array object.  Parallel arrays have specially
    // constructed type objects, so we can only perform the inlining if we
    // already have one of these type objects.
    types::StackTypeSet *returnTypes = getInlineReturnTypeSet();
    if (returnTypes->getKnownTypeTag() != JSVAL_TYPE_OBJECT)
        return InliningStatus_NotInlined;
    if (returnTypes->getObjectCount() != 1)
        return InliningStatus_NotInlined;
    types::TypeObject *typeObject = returnTypes->getTypeObject(0);

    // Pop the arguments and |this|.
    Vector<MPassArg *> args(cx);
    MPassArg *oldThis;
    MDefinition *discardFun;

    popFormals(argc, &discardFun, &oldThis, &args);

    // Adjust argc according to how many arguments we're discarding.
    argc -= discards;

    // Create the call and add in the non-this arguments.
    uint32_t targetArgs = argc;
    if (target && !target->isNative())
        targetArgs = Max<uint32_t>(target->nargs, argc);

    MCall *call = MCall::New(target, targetArgs + 1, argc, false, ctorTypes);
    if (!call)
        return InliningStatus_Error;

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

    // Add explicit arguments.
    // Skip addArg(0) because it is reserved for this
    for (int32_t i = argc - 1; i >= 0; i--)
        call->addArg(i + 1, args[i + discards]);

    // Place an MPrepareCall before the first passed argument, before we
    // potentially perform rearrangement.
    MPrepareCall *start = new MPrepareCall;
    oldThis->block()->insertBefore(oldThis, start);
    call->initPrepareCall(start);

    // Discard the old |this| and extra arguments.
    for (int32_t i = 0; i < discards; i++)
        UnwrapAndDiscardPassArg(args[i]);
    UnwrapAndDiscardPassArg(oldThis);

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
    if (!resumeAfter(newObject))
        return InliningStatus_Error;

    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineDenseArray(uint32_t argc, bool constructing)
{
    if (constructing || argc != 1)
        return InliningStatus_NotInlined;

    // For now, in seq. mode we just call the C function.  In
    // par. mode we use inlined MIR.
    ExecutionMode executionMode = info().executionMode();
    switch (executionMode) {
      case SequentialExecution: return inlineDenseArrayForSequentialExecution(argc);
      case ParallelExecution: return inlineDenseArrayForParallelExecution(argc);
    }

    JS_NOT_REACHED("unknown ExecutionMode");
}

IonBuilder::InliningStatus
IonBuilder::inlineDenseArrayForSequentialExecution(uint32_t argc)
{
    // not yet implemented; in seq. mode the C function is not so bad
    return InliningStatus_NotInlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineDenseArrayForParallelExecution(uint32_t argc)
{
    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

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

    MParNewDenseArray *newObject = new MParNewDenseArray(graph().parSlice(),
                                                         argv[1],
                                                         templateObject);
    current->add(newObject);
    current->push(newObject);

    return InliningStatus_Inlined;
}

IonBuilder::InliningStatus
IonBuilder::inlineThrowError(uint32_t argc, bool constructing)
{
    // In Parallel Execution, convert %ThrowError() into a bailout.

    if (constructing)
        return InliningStatus_NotInlined;

    ExecutionMode executionMode = info().executionMode();
    switch (executionMode) {
      case SequentialExecution:
        return InliningStatus_NotInlined;
      case ParallelExecution:
        break;
    }

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

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
IonBuilder::inlineDump(uint32_t argc, bool constructing)
{
    // In Parallel Execution, call ParDump.  We just need a debugging
    // aid!

    if (constructing)
        return InliningStatus_NotInlined;

    ExecutionMode executionMode = info().executionMode();
    switch (executionMode) {
      case SequentialExecution:
        return InliningStatus_NotInlined;
      case ParallelExecution:
        break;
    }

    MDefinitionVector argv;
    if (!discardCall(argc, argv, current))
        return InliningStatus_Error;

    MParDump *dump = new MParDump(argv[1]);
    current->add(dump);

    MConstant *udef = MConstant::New(UndefinedValue());
    current->add(udef);
    current->push(udef);

    return InliningStatus_Inlined;
}

} // namespace ion
} // namespace js
