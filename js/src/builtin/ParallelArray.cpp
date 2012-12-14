/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=99 ft=cpp:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "builtin/ParallelArray.h"

#include "jsapi.h"
#include "jsobj.h"
#include "jsarray.h"

#include "vm/String.h"
#include "vm/GlobalObject.h"
#include "vm/ThreadPool.h"

#include "jsinterpinlines.h"
#include "jsobjinlines.h"
#include "jsarrayinlines.h"
#include "vm/ForkJoin-inl.h"

#include "ion/Ion.h"
#include "ion/IonCompartment.h"
#include "ion/ParallelArrayAnalysis.h"

using namespace js;
using namespace js::parallel;
using namespace js::ion;

//
// Debug spew
//

enum SpewChannel {
    SpewOps,
    SpewTypes,
    NumSpewChannels
};

static const char *
ExecutionStatusToString(ExecutionStatus status)
{
    switch (status) {
      case ExecutionDisqualified:
        return "disqualified";
      case ExecutionBailout:
        return "bailout";
      case ExecutionFatal:
        return "fatal";
      case ExecutionSucceeded:
        return "success";
    }
    return "(unknown status)";
}

#ifdef DEBUG

static bool
SpewColorable()
{
    // Only spew colors on xterm-color to not screw up emacs.
    static bool colorable = false;
    static bool checked = false;
    if (!checked) {
        checked = true;
        const char *env = getenv("TERM");
        if (!env)
            return false;
        if (strcmp(env, "xterm-color") == 0 || strcmp(env, "xterm-256color") == 0)
            colorable = true;
    }
    return colorable;
}

static inline const char *
SpewColorReset()
{
    if (!SpewColorable())
        return "";
    return "\x1b[0m";
}

static inline const char *
SpewColorRaw(const char *colorCode) {
    if (!SpewColorable())
        return "";
    return colorCode;
}

static inline const char *
SpewColorRed() { return SpewColorRaw("\x1b[31m"); }
static inline const char *
SpewColorGreen() { return SpewColorRaw("\x1b[32m"); }
static inline const char *
SpewColorYellow() { return SpewColorRaw("\x1b[33m"); }

static bool
IsSpewActive(SpewChannel channel)
{
    // Note: in addition, setting PAFLAGS=trace will cause
    // special instructions to be written into the generated
    // code that look like:
    //
    // mov 0xDEADBEEF, eax
    // mov N, eax

    static bool active[NumSpewChannels];
    static bool checked = false;
    if (!checked) {
        checked = true;
        PodArrayZero(active);
        const char *env = getenv("PAFLAGS");
        if (!env)
            return false;
        if (strstr(env, "ops"))
            active[SpewOps] = true;
        if (strstr(env, "types"))
            active[SpewTypes] = true;
        if (strstr(env, "full")) {
            for (uint32_t i = 0; i < NumSpewChannels; i++)
                active[i] = true;
        }
    }
    return active[channel];
}

static void
Spew(JSContext *cx, SpewChannel channel, const char *fmt, ...)
{
    if (!IsSpewActive(channel))
        return;

    jsbytecode *pc;
    JSScript *script = cx->stack.currentScript(&pc);
    if (!script || !pc)
        return;

    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[ParallelArray] %s:%u: ", script->filename, PCToLineNumber(script, pc));
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static void
SpewExecution(JSContext *cx, const char *op, ExecutionStatus status)
{
    const char *statusColor;
    switch (status) {
      case ExecutionDisqualified:
      case ExecutionFatal:
        statusColor = SpewColorRed();
        break;
      case ExecutionBailout:
        statusColor = SpewColorYellow();
        break;
      case ExecutionSucceeded:
        statusColor = SpewColorGreen();
        break;
    }
    Spew(cx, SpewOps, "%s: %s%s%s", op, statusColor,
         ExecutionStatusToString(status), SpewColorReset());
}

#else

static bool IsSpewActive(SpewChannel channel) { return false; }
static void Spew(JSContext *cx, SpewChannel channel, const char *fmt, ...) {}
static void SpewExecution(JSContext *cx, const char *op, const ExecutionMode &mode,
                          ExecutionStatus status) {}

#endif

static ExecutionStatus
ToExecutionStatus(JSContext *cx, const char *opName, ParallelResult pr)
{
    ExecutionStatus status;
    switch (pr) {
      case TP_SUCCESS:
        status = ExecutionSucceeded;
        break;

      case TP_RETRY_SEQUENTIALLY:
        status = ExecutionBailout;
        break;

      case TP_FATAL:
        status = ExecutionFatal;
        break;
    }

#ifdef DEBUG
    SpewExecution(cx, opName, status);
#endif

    return status;
}

// Can only enter callees with a valid IonScript.
template <uint32_t maxArgc>
class FastestIonInvoke
{
    EnterIonCode enter_;
    void *jitcode_;
    void *calleeToken_;
    Value argv_[maxArgc + 2];
    uint32_t argc_;

  public:
    Value *args;

    FastestIonInvoke(JSContext *cx, HandleObject fun, uint32_t argc)
      : argc_(argc),
        args(argv_ + 2)
    {
        JS_ASSERT(argc <= maxArgc + 2);
        JS_ASSERT(fun->isFunction());

        // Set 'callee' and 'this'.
        RootedFunction callee(cx, fun->toFunction());
        argv_[0] = ObjectValue(*callee);
        argv_[1] = UndefinedValue();

        // Find JIT code pointer.
        IonScript *ion = callee->nonLazyScript()->parallelIonScript();
        IonCode *code = ion->method();
        jitcode_ = code->raw();
        enter_ = cx->compartment->ionCompartment()->enterJIT();
        calleeToken_ = CalleeToToken(callee);
    }

    bool invoke() {
        Value result;
        enter_(jitcode_, argc_ + 1, argv_ + 1, NULL, calleeToken_, &result);
        return !result.isMagic();
    }
};

struct AutoEnterParallelWarmup
{
    AutoEnterParallelWarmup(ParallelCompileContext *compileContext) {
        ion::js_IonOptions.startParallelWarmup(compileContext);
    }
    ~AutoEnterParallelWarmup() {
        ion::js_IonOptions.finishParallelWarmup();
    }
};

static inline bool
InWarmup() {
    return ion::js_IonOptions.parallelWarmupContext != NULL;
}

class ParallelDo : public ForkJoinOp
{
    static const uint32_t baseArgc = 3;

    JSContext *cx_;
    HeapPtrObject fun_;
    Value *funArgs_;
    uint32_t funArgc_;

  public:
    Vector<JSScript *> pendingInvalidations;

    ParallelDo(JSContext *cx, HandleObject fun, Value *funArgs, uint32_t funArgc)
      : cx_(cx),
        fun_(fun),
        funArgs_(funArgs),
        funArgc_(funArgc),
        pendingInvalidations(cx)
    { }

    ExecutionStatus apply() {
        Spew(cx_, SpewOps, "ParallelDo: attempting parallel compilation");
        if (!ion::IsEnabled(cx_))
            return ExecutionDisqualified;

        if (!pendingInvalidations.resize(ForkJoinSlices(cx_)))
            return ExecutionFatal;

        MethodStatus status = compileForParallelExecution();
        if (status == Method_Error)
            return ExecutionFatal;
        if (status != Method_Compiled)
            return ExecutionDisqualified;

        Spew(cx_, SpewOps, "ParallelDo: entering parallel section");
        ParallelResult pr = js::ExecuteForkJoinOp(cx_, *this);
        return ToExecutionStatus(cx_, "ParallelDo", pr);
    }

    MethodStatus compileForParallelExecution() {
        // The kernel should be a self-hosted function.
        if (!fun_->isFunction())
            return Method_Skipped;

        RootedFunction callee(cx_, fun_->toFunction());

        if (!callee->isInterpreted() || !callee->isSelfHostedBuiltin())
            return Method_Skipped;

        // Ensure that the function is analyzed by TI.
        bool hasIonScript;
        {
            AutoAssertNoGC nogc;
            hasIonScript = callee->getOrCreateScript(cx_)->hasParallelIonScript();
        }

        if (!hasIonScript) {
            // If the script has not been compiled in parallel, then type
            // inference will have no particular information.  In that case,
            // we need to do a few "warm-up" iterations to give type inference
            // some data to work with and to record all functions called.
            ParallelCompileContext compileContext(cx_);
            {
                AutoEnterParallelWarmup enter(&compileContext);
                if (!warmup())
                    return Method_Error;
            }

            // After warming up, compile the outer kernel as a special
            // self-hosted kernel that can unsafely write to the buffer.
            Spew(cx_, SpewOps, "ParallelDo: compilation");
            return compileContext.compileKernelAndInvokedFunctions(callee);
        }

        return Method_Compiled;
    }

    bool warmup() {
        JS_ASSERT(InWarmup());

        uint32_t slices = ForkJoinSlices(cx_);
        for (uint32_t id = 0; id < slices; id++) {
            Spew(cx_, SpewOps, "ParallelDo: warmup %u/%u", id, slices);

            InvokeArgsGuard args;
            if (!cx_->stack.pushInvokeArgs(cx_, baseArgc + funArgc_, &args))
                return false;

            args.setCallee(ObjectValue(*fun_));
            args.setThis(UndefinedValue());

            // run the warmup with each of the ids
            args[0].setInt32(id);
            args[1].setInt32(slices);
            args[2].setBoolean(true); // warmup
            for (uint32_t i = 0; i < funArgc_; i++)
                args[baseArgc + i] = funArgs_[i];

            if (!Invoke(cx_, args))
                return false;
        }

        return true;
    }

    virtual bool parallel(ForkJoinSlice &slice) {
        // Setting maximum argc at 10, since it is more than we
        // actually use in practice.  If you add parameters, you may
        // have to adjust this.
        js::PerThreadData *pt = slice.perThreadData;
        RootedObject fun(pt, fun_);
        FastestIonInvoke<10> fii(cx_, fun, funArgc_ + baseArgc);

        // The first 3 arguments: buffer, thread id, and number of threads.
        fii.args[0] = Int32Value(slice.sliceId);
        fii.args[1] = Int32Value(slice.numSlices);
        fii.args[2] = BooleanValue(false); // warmup
        for (uint32_t i = 0; i < funArgc_; i++)
            fii.args[baseArgc + i] = funArgs_[i];

        bool ok = fii.invoke();
        if (ok) {
            JS_ASSERT(!slice.abortedScript);
            pendingInvalidations[slice.sliceId] = NULL;
        } else {
            JS_ASSERT(slice.abortedScript);
            JSScript *script = slice.abortedScript;
            pendingInvalidations[slice.sliceId] = script;
        }
        return ok;
    }
};

static inline bool
HasScript(Vector<types::RecompileInfo> &scripts, JSScript *script)
{
    for (uint32_t i = 0; i < scripts.length(); i++) {
        if (scripts[i] == script->parallelIonScript()->recompileInfo())
            return true;
    }
    return false;
}

static ExecutionStatus Do1(JSContext *cx, CallArgs &args);

ExecutionStatus
js::parallel::Do(JSContext *cx, CallArgs &args)
{
    ExecutionStatus status = Do1(cx, args);

    if (status != ExecutionFatal) {
        if (args[1].isObject()) {
            RootedObject feedback(cx, &args[1].toObject());
            if (feedback && feedback->isFunction()) {
                const char *statusCString = ExecutionStatusToString(status);
                RootedString statusString(cx, JS_NewStringCopyZ(cx, statusCString));
                InvokeArgsGuard args1;
                if (!cx->stack.pushInvokeArgs(cx, 1, &args1))
                    return ExecutionFatal;
                args1.setCallee(ObjectValue(*feedback));
                args1.setThis(UndefinedValue());
                args1[0].setString(statusString);
                if (!Invoke(cx, args1))
                    return ExecutionFatal;
            }
        }
    }

    return status;
}

static ExecutionStatus
Do1(JSContext *cx, CallArgs &args)
{
    JS_ASSERT(args[0].isObject());
    JS_ASSERT(args[0].toObject().isFunction());

    // Nesting is not allowed.
    if (InWarmup()) {
        args.rval().setBoolean(false);
        return ExecutionDisqualified;
    }

    RootedObject fun(cx, &args[0].toObject());
    ParallelDo op(cx, fun, args.array() + 2, args.length() - 2);
    ExecutionStatus status = op.apply();

    // If we bailed out, invalidate the kernel to be reanalyzed (all the way
    // down) and recompiled.
    //
    // TODO: This is too coarse grained.
    if (status == ExecutionSucceeded) {
        args.rval().setBoolean(true);
    } else {
        if (status == ExecutionBailout) {
            Vector<types::RecompileInfo> invalid(cx);
            for (uint32_t i = 0; i < op.pendingInvalidations.length(); i++) {
                JSScript *script = op.pendingInvalidations[i];
                if (script && !HasScript(invalid, script)) {
                    JS_ASSERT(script->hasParallelIonScript());
                    if (!invalid.append(script->parallelIonScript()->recompileInfo()))
                        return ExecutionFatal;
                }
            }
            Invalidate(cx, invalid);
        }

        args.rval().setBoolean(false);
    }

    return status;
}

//
// ParallelArrayObject
//

FixedHeapPtr<PropertyName> ParallelArrayObject::ctorNames[NumCtors];

// TODO: non-generic self hosted
JSFunctionSpec ParallelArrayObject::methods[] = {
    { "map",       JSOP_NULLWRAPPER, 2, 0, "ParallelArrayMap"       },
    { "reduce",    JSOP_NULLWRAPPER, 2, 0, "ParallelArrayReduce"    },
    { "scan",      JSOP_NULLWRAPPER, 2, 0, "ParallelArrayScan"      },
    { "scatter",   JSOP_NULLWRAPPER, 5, 0, "ParallelArrayScatter"   },
    { "filter",    JSOP_NULLWRAPPER, 2, 0, "ParallelArrayFilter"    },
    { "partition", JSOP_NULLWRAPPER, 1, 0, "ParallelArrayPartition" },
    { "flatten",   JSOP_NULLWRAPPER, 0, 0, "ParallelArrayFlatten" },
    /*{ "get",      JSOP_NULLWRAPPER, 1, 0, "ParallelArrayGet" },*/
    { "toString", JSOP_NULLWRAPPER, 0, 0, "ParallelArrayToString" },
    JS_FS_END
};

Class ParallelArrayObject::protoClass = {
    "ParallelArray",
    JSCLASS_HAS_CACHED_PROTO(JSProto_ParallelArray),
    JS_PropertyStub,         // addProperty
    JS_PropertyStub,         // delProperty
    JS_PropertyStub,         // getProperty
    JS_StrictPropertyStub,   // setProperty
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub
};

Class ParallelArrayObject::class_ = {
    "ParallelArray",
    JSCLASS_HAS_CACHED_PROTO(JSProto_ParallelArray),
    JS_PropertyStub,         // addProperty
    JS_PropertyStub,         // delProperty
    JS_PropertyStub,         // getProperty
    JS_StrictPropertyStub,   // setProperty
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub
};

/*static*/ bool
ParallelArrayObject::initProps(JSContext *cx, HandleObject obj)
{
    RootedValue undef(cx, UndefinedValue());
    RootedValue zero(cx, Int32Value(0));

    if (!JSObject::setProperty(cx, obj, obj, cx->names().buffer, &undef, true))
        return false;
    if (!JSObject::setProperty(cx, obj, obj, cx->names().offset, &zero, true))
        return false;
    if (!JSObject::setProperty(cx, obj, obj, cx->names().shape, &undef, true))
        return false;
    if (!JSObject::setProperty(cx, obj, obj, cx->names().get, &undef, true))
        return false;

    return true;
}

/*static*/ JSBool
ParallelArrayObject::construct(JSContext *cx, unsigned argc, Value *vp)
{
    RootedFunction ctor(cx, getConstructor(cx, argc));
    if (!ctor)
        return false;
    CallArgs args = CallArgsFromVp(argc, vp);
    return constructHelper(cx, &ctor, args);
}


/* static */ JSFunction *
ParallelArrayObject::getConstructor(JSContext *cx, unsigned argc)
{
    RootedPropertyName ctorName(cx, ctorNames[js::Min(argc, NumCtors - 1)]);
    RootedValue ctorValue(cx);
    if (!cx->global()->getIntrinsicValue(cx, ctorName, &ctorValue))
        return NULL;
    return ctorValue.toObject().toFunction();
}

/*static*/ JSObject *
ParallelArrayObject::newInstance(JSContext *cx)
{
    gc::AllocKind kind = gc::GetGCObjectKind(NumFixedSlots);
    RootedObject result(cx, NewBuiltinClassInstance(cx, &class_, kind));
    if (!result)
        return NULL;

    // Add in the basic PA properties now with default values:
    if (!initProps(cx, result))
        return NULL;

    return result;
}

/*static*/ JSBool
ParallelArrayObject::constructHelper(JSContext *cx, MutableHandleFunction ctor, CallArgs &args0)
{
    RootedObject result(cx, newInstance(cx));
    if (!result)
        return false;

    if (cx->typeInferenceEnabled()) {
        jsbytecode *pc;
        RootedScript script(cx, cx->stack.currentScript(&pc));
        if (script) {
            if (ctor->isCloneAtCallsite()) {
                ctor.set(CloneFunctionAtCallsite(cx, ctor, script, pc));
                if (!ctor)
                    return false;
            }

            // Create the type object for the PA.  Add in the current
            // properties as definite properties if this type object is newly
            // created.  To tell if it is newly created, we check whether it
            // has any properties yet or not, since any returned type object
            // must have been created by this same C++ code and hence would
            // already have properties if it had been returned before.
            types::TypeObject *paTypeObject =
                types::TypeScript::InitObject(cx, script, pc, JSProto_ParallelArray);
            if (!paTypeObject)
                return false;
            if (paTypeObject->getPropertyCount() == 0) {
                if (!paTypeObject->addDefiniteProperties(cx, result))
                    return false;
                JS_ASSERT(paTypeObject->getPropertyCount() == NumFixedSlots);
            }
            result->setType(paTypeObject);
        }
    }

    InvokeArgsGuard args;
    if (!cx->stack.pushInvokeArgs(cx, args0.length(), &args))
        return false;

    args.setCallee(ObjectValue(*ctor));
    args.setThis(ObjectValue(*result));

    for (uint32_t i = 0; i < args0.length(); i++)
        args[i] = args0[i];

    if (!Invoke(cx, args))
        return false;

    args0.rval().setObject(*result);
    return true;
}

JSObject *
ParallelArrayObject::initClass(JSContext *cx, HandleObject obj)
{
    JS_ASSERT(obj->isNative());

    // Cache constructor names.
    {
        const char *ctorStrs[NumCtors] = { "ParallelArrayConstruct0",
                                           "ParallelArrayConstruct1",
                                           "ParallelArrayConstruct2",
                                           "ParallelArrayConstruct3" };
        for (uint32_t i = 0; i < NumCtors; i++) {
            JSAtom *atom = Atomize(cx, ctorStrs[i], strlen(ctorStrs[i]), InternAtom);
            if (!atom)
                return NULL;
            ctorNames[i].init(atom->asPropertyName());
        }
    }

    Rooted<GlobalObject *> global(cx, &obj->asGlobal());

    RootedObject proto(cx, global->createBlankPrototype(cx, &protoClass));
    if (!proto)
        return NULL;

    JSProtoKey key = JSProto_ParallelArray;
    RootedFunction ctor(cx, global->createConstructor(cx, construct,
                                                      cx->names().ParallelArray, 0));
    if (!ctor ||
        !LinkConstructorAndPrototype(cx, ctor, proto) ||
        !DefinePropertiesAndBrand(cx, proto, NULL, methods) ||
        !DefineConstructorAndPrototype(cx, global, key, ctor, proto))
    {
        return NULL;
    }

    // Define the length getter.
    {
        const char lengthStr[] = "ParallelArrayLength";
        JSAtom *atom = Atomize(cx, lengthStr, strlen(lengthStr));
        if (!atom)
            return NULL;
        Rooted<PropertyName *> lengthProp(cx, atom->asPropertyName());
        RootedValue lengthValue(cx);
        if (!cx->global()->getIntrinsicValue(cx, lengthProp, &lengthValue))
            return NULL;
        RootedObject lengthGetter(cx, &lengthValue.toObject());
        if (!lengthGetter)
            return NULL;

        RootedId lengthId(cx, AtomToId(cx->names().length));
        unsigned flags = JSPROP_PERMANENT | JSPROP_SHARED | JSPROP_GETTER;
        RootedValue value(cx, UndefinedValue());
        if (!DefineNativeProperty(cx, proto, lengthId, value,
                                  JS_DATA_TO_FUNC_PTR(PropertyOp, lengthGetter.get()), NULL,
                                  flags, 0, 0))
        {
            return NULL;
        }
    }

    return proto;
}

bool
ParallelArrayObject::is(const Value &v)
{
    return v.isObject() && v.toObject().hasClass(&class_);
}

JSObject *
js_InitParallelArrayClass(JSContext *cx, js::HandleObject obj)
{
    return ParallelArrayObject::initClass(cx, obj);
}
