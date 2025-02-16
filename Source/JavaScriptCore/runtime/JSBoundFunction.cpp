/*
 * Copyright (C) 2011, 2016-2017 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "JSBoundFunction.h"

#include "ExecutableBaseInlines.h"
#include "GetterSetter.h"
#include "JSGlobalObject.h"
#include "JSCInlines.h"

namespace JSC {

const ClassInfo JSBoundFunction::s_info = { "Function", &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSBoundFunction) };

EncodedJSValue JSC_HOST_CALL boundThisNoArgsFunctionCall(JSGlobalObject* globalObject, CallFrame* callFrame)
{
    JSBoundFunction* boundFunction = jsCast<JSBoundFunction*>(callFrame->jsCallee());

    MarkedArgumentBuffer args;
    for (unsigned i = 0; i < callFrame->argumentCount(); ++i)
        args.append(callFrame->uncheckedArgument(i));
    RELEASE_ASSERT(!args.hasOverflowed());

    JSFunction* targetFunction = jsCast<JSFunction*>(boundFunction->targetFunction());
    ExecutableBase* executable = targetFunction->executable();
    if (executable->hasJITCodeForCall()) {
        // Force the executable to cache its arity entrypoint.
        executable->entrypointFor(CodeForCall, MustCheckArity);
    }
    CallData callData;
    CallType callType = getCallData(globalObject->vm(), targetFunction, callData);
    ASSERT(callType != CallType::None);
    return JSValue::encode(call(callFrame, targetFunction, callType, callData, boundFunction->boundThis(), args));
}

EncodedJSValue JSC_HOST_CALL boundFunctionCall(JSGlobalObject* globalObject, CallFrame* callFrame)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    JSBoundFunction* boundFunction = jsCast<JSBoundFunction*>(callFrame->jsCallee());

    JSArray* boundArgs = boundFunction->boundArgs();

    MarkedArgumentBuffer args;
    if (boundArgs) {
        for (unsigned i = 0; i < boundArgs->length(); ++i)
            args.append(boundArgs->getIndexQuickly(i));
    }
    for (unsigned i = 0; i < callFrame->argumentCount(); ++i)
        args.append(callFrame->uncheckedArgument(i));
    if (UNLIKELY(args.hasOverflowed())) {
        throwOutOfMemoryError(callFrame, scope);
        return encodedJSValue();
    }

    JSObject* targetFunction = boundFunction->targetFunction();
    CallData callData;
    CallType callType = getCallData(vm, targetFunction, callData);
    ASSERT(callType != CallType::None);
    RELEASE_AND_RETURN(scope, JSValue::encode(call(callFrame, targetFunction, callType, callData, boundFunction->boundThis(), args)));
}

EncodedJSValue JSC_HOST_CALL boundThisNoArgsFunctionConstruct(JSGlobalObject* globalObject, CallFrame* callFrame)
{
    JSBoundFunction* boundFunction = jsCast<JSBoundFunction*>(callFrame->jsCallee());

    MarkedArgumentBuffer args;
    for (unsigned i = 0; i < callFrame->argumentCount(); ++i)
        args.append(callFrame->uncheckedArgument(i));
    RELEASE_ASSERT(!args.hasOverflowed());

    JSFunction* targetFunction = jsCast<JSFunction*>(boundFunction->targetFunction());
    ConstructData constructData;
    ConstructType constructType = getConstructData(globalObject->vm(), targetFunction, constructData);
    ASSERT(constructType != ConstructType::None);
    return JSValue::encode(construct(callFrame, targetFunction, constructType, constructData, args));
}

EncodedJSValue JSC_HOST_CALL boundFunctionConstruct(JSGlobalObject* globalObject, CallFrame* callFrame)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    JSBoundFunction* boundFunction = jsCast<JSBoundFunction*>(callFrame->jsCallee());

    JSArray* boundArgs = boundFunction->boundArgs();

    MarkedArgumentBuffer args;
    if (boundArgs) {
        for (unsigned i = 0; i < boundArgs->length(); ++i)
            args.append(boundArgs->getIndexQuickly(i));
    }
    for (unsigned i = 0; i < callFrame->argumentCount(); ++i)
        args.append(callFrame->uncheckedArgument(i));
    if (UNLIKELY(args.hasOverflowed())) {
        throwOutOfMemoryError(callFrame, scope);
        return encodedJSValue();
    }

    JSObject* targetFunction = boundFunction->targetFunction();
    ConstructData constructData;
    ConstructType constructType = getConstructData(vm, targetFunction, constructData);
    ASSERT(constructType != ConstructType::None);
    RELEASE_AND_RETURN(scope, JSValue::encode(construct(callFrame, targetFunction, constructType, constructData, args)));
}

EncodedJSValue JSC_HOST_CALL isBoundFunction(JSGlobalObject* globalObject, CallFrame* callFrame)
{
    return JSValue::encode(JSValue(static_cast<bool>(jsDynamicCast<JSBoundFunction*>(globalObject->vm(), callFrame->uncheckedArgument(0)))));
}

EncodedJSValue JSC_HOST_CALL hasInstanceBoundFunction(JSGlobalObject*, CallFrame* callFrame)
{
    JSBoundFunction* boundObject = jsCast<JSBoundFunction*>(callFrame->uncheckedArgument(0));
    JSValue value = callFrame->uncheckedArgument(1);

    return JSValue::encode(jsBoolean(boundObject->targetFunction()->hasInstance(callFrame, value)));
}

inline Structure* getBoundFunctionStructure(VM& vm, ExecState* exec, JSGlobalObject* globalObject, JSObject* targetFunction)
{
    auto scope = DECLARE_THROW_SCOPE(vm);
    JSValue prototype = targetFunction->getPrototype(vm, exec);
    RETURN_IF_EXCEPTION(scope, nullptr);
    JSFunction* targetJSFunction = jsDynamicCast<JSFunction*>(vm, targetFunction);

    // We only cache the structure of the bound function if the bindee is a JSFunction since there
    // isn't any good place to put the structure on Internal Functions.
    if (targetJSFunction) {
        Structure* structure = targetJSFunction->rareData(vm)->getBoundFunctionStructure();
        if (structure && structure->storedPrototype() == prototype && structure->globalObject() == globalObject)
            return structure;
    }

    Structure* result = globalObject->boundFunctionStructure();

    // It would be nice if the structure map was keyed global objects in addition to the other things. Unfortunately, it is not
    // currently. Whoever works on caching structure changes for prototype transitions should consider this problem as well.
    // See: https://bugs.webkit.org/show_bug.cgi?id=152738
    if (prototype.isObject() && prototype.getObject()->globalObject(vm) == globalObject) {
        result = vm.structureCache.emptyStructureForPrototypeFromBaseStructure(globalObject, prototype.getObject(), result);
        ASSERT_WITH_SECURITY_IMPLICATION(result->globalObject() == globalObject);
    } else
        result = Structure::create(vm, globalObject, prototype, result->typeInfo(), result->classInfo());

    if (targetJSFunction)
        targetJSFunction->rareData(vm)->setBoundFunctionStructure(vm, result);

    return result;
}

JSBoundFunction* JSBoundFunction::create(VM& vm, ExecState* exec, JSGlobalObject* globalObject, JSObject* targetFunction, JSValue boundThis, JSArray* boundArgs, int length, const String& name)
{
    auto scope = DECLARE_THROW_SCOPE(vm);
    ConstructData constructData;
    ConstructType constructType = JSC::getConstructData(vm, targetFunction, constructData);
    bool canConstruct = constructType != ConstructType::None;
    
    bool slowCase = boundArgs || !getJSFunction(targetFunction);
    
    NativeExecutable* executable = vm.getHostFunction(
        slowCase ? boundFunctionCall : boundThisNoArgsFunctionCall,
        slowCase ? NoIntrinsic : BoundThisNoArgsFunctionCallIntrinsic,
        canConstruct ? (slowCase ? boundFunctionConstruct : boundThisNoArgsFunctionConstruct) : callHostFunctionAsConstructor, nullptr,
        name);
    Structure* structure = getBoundFunctionStructure(vm, exec, globalObject, targetFunction);
    RETURN_IF_EXCEPTION(scope, nullptr);
    JSBoundFunction* function = new (NotNull, allocateCell<JSBoundFunction>(vm.heap)) JSBoundFunction(vm, globalObject, structure, targetFunction, boundThis, boundArgs);

    function->finishCreation(vm, executable, length);
    return function;
}

bool JSBoundFunction::customHasInstance(JSObject* object, ExecState* exec, JSValue value)
{
    return jsCast<JSBoundFunction*>(object)->m_targetFunction->hasInstance(exec, value);
}

JSBoundFunction::JSBoundFunction(VM& vm, JSGlobalObject* globalObject, Structure* structure, JSObject* targetFunction, JSValue boundThis, JSArray* boundArgs)
    : Base(vm, globalObject, structure)
    , m_targetFunction(vm, this, targetFunction)
    , m_boundThis(vm, this, boundThis)
    , m_boundArgs(vm, this, boundArgs, WriteBarrier<JSArray>::MayBeNull)
{
}

JSArray* JSBoundFunction::boundArgsCopy(ExecState* exec)
{
    VM& vm = exec->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    JSArray* result = constructEmptyArray(exec, nullptr, globalObject());
    RETURN_IF_EXCEPTION(scope, nullptr);
    for (unsigned i = 0; i < m_boundArgs->length(); ++i) {
        result->push(exec, m_boundArgs->getIndexQuickly(i));
        RETURN_IF_EXCEPTION(scope, nullptr);
    }
    return result;
}

void JSBoundFunction::finishCreation(VM& vm, NativeExecutable* executable, int length)
{
    String name; // We lazily create our 'name' string property.
    Base::finishCreation(vm, executable, length, name);
    ASSERT(inherits(vm, info()));
}

void JSBoundFunction::visitChildren(JSCell* cell, SlotVisitor& visitor)
{
    JSBoundFunction* thisObject = jsCast<JSBoundFunction*>(cell);
    ASSERT_GC_OBJECT_INHERITS(thisObject, info());
    Base::visitChildren(thisObject, visitor);

    visitor.append(thisObject->m_targetFunction);
    visitor.append(thisObject->m_boundThis);
    visitor.append(thisObject->m_boundArgs);
}

} // namespace JSC
