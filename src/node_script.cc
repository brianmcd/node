// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "node.h"
#include "node_script.h"
#include <assert.h>

namespace node {

using v8::Context;
using v8::Script;
using v8::Value;
using v8::Handle;
using v8::HandleScope;
using v8::Object;
using v8::Arguments;
using v8::ThrowException;
using v8::TryCatch;
using v8::String;
using v8::Exception;
using v8::Local;
using v8::Array;
using v8::Persistent;
using v8::Integer;
using v8::Function;
using v8::FunctionTemplate;
using v8::AccessType;
using v8::AccessorInfo;
using v8::Boolean;
using v8::None;
using v8::ObjectTemplate;

class WrappedContext : ObjectWrap {
 public:
  static void Initialize(Handle<Object> target);
  static Handle<Value> New(const Arguments& args);

  Persistent<Context> GetV8Context();
  static Local<Object> NewInstance(const Arguments& args);// TODO: only pass sandbox, not all args.
  static bool InstanceOf(Handle<Value> value);

 //protected: TODO

  static Persistent<FunctionTemplate> constructor_template;

  Persistent<Context> context_;
  Persistent<Object>  sandbox_;
  Persistent<Object>  proxy_global_;
  Persistent<Object>  host_;

  static void HostObjectWeakCallback(Persistent<Value> host, void* param) {
    host.ClearWeak();
    host->ToObject()->SetPointerInInternalField(0, NULL);
    host.Dispose();
    host.Clear();
  }

  WrappedContext(Local<Object> sandbox, Local<Object> host);
  ~WrappedContext();

  Persistent<Context> CreateV8Context() {
    HandleScope scope;
    Local<FunctionTemplate> ftmpl = FunctionTemplate::New();
    ftmpl->SetHiddenPrototype(true);
    ftmpl->SetClassName(sandbox_->GetConstructorName());
    Local<ObjectTemplate> otmpl = ftmpl->InstanceTemplate();
    otmpl->SetNamedPropertyHandler(GlobalPropertyGetter,
                                   GlobalPropertySetter,
                                   GlobalPropertyQuery,
                                   GlobalPropertyDeleter,
                                   GlobalPropertyEnumerator,
                                   host_);
    otmpl->SetAccessCheckCallbacks(GlobalPropertyNamedAccessCheck,
                                   GlobalPropertyIndexedAccessCheck);
    return Context::New(NULL, otmpl);
  }


  static bool GlobalPropertyNamedAccessCheck(Local<Object> host,
                                             Local<Value>  key,
                                             AccessType    type,
                                             Local<Value>  data) {
    return true;
  }

  static bool GlobalPropertyIndexedAccessCheck(Local<Object> host,
                                               uint32_t      key,
                                               AccessType    type,
                                               Local<Value>  data) {
    return true;
  }

  static Handle<Value> GlobalPropertyGetter(Local<String> property,
                                            const AccessorInfo &access_info) {
    HandleScope scope;
    Local<Object> host = access_info.Data()->ToObject();
    WrappedContext *ctx = ObjectWrap::Unwrap<WrappedContext>(host);
    if (ctx == NULL) return scope.Close(v8::Undefined());  // TODO: explain these
    Local<Value> rv = ctx->sandbox_->GetRealNamedProperty(property);
    if (rv.IsEmpty()) {
      rv = ctx->proxy_global_->GetRealNamedProperty(property);
    }
    return scope.Close(rv);
  }

  static Handle<Value> GlobalPropertySetter(Local<String> property,
                                            Local<Value> value,
                                            const AccessorInfo &access_info) {
    HandleScope scope;
    Local<Object> host = access_info.Data()->ToObject();
    WrappedContext *ctx = ObjectWrap::Unwrap<WrappedContext>(host);
    if (ctx == NULL) return scope.Close(v8::Undefined());
    ctx->sandbox_->Set(property, value);
    host->Set(property, value); // TODO: For backwards compatibility.
    return scope.Close(value);
  }

  static Handle<Integer> GlobalPropertyQuery(Local<String> property,
                                             const AccessorInfo &access_info) {
    HandleScope scope;
    Local<Object> host = access_info.Data()->ToObject();
    WrappedContext *ctx = ObjectWrap::Unwrap<WrappedContext>(host);
    if (ctx == NULL) return scope.Close(Handle<Integer>());
    if (!ctx->sandbox_->GetRealNamedProperty(property).IsEmpty() ||
        !ctx->proxy_global_->GetRealNamedProperty(property).IsEmpty()) {
        return scope.Close(Integer::New(None));
    }
    return scope.Close(Handle<Integer>());
  }

  static Handle<Boolean> GlobalPropertyDeleter(Local<String> property,
                                               const AccessorInfo &access_info) {
    HandleScope scope;
    Local<Object> host = access_info.Data()->ToObject();
    WrappedContext *ctx = ObjectWrap::Unwrap<WrappedContext>(host);
    if (ctx == NULL) return scope.Close(v8::False());
    bool success = ctx->sandbox_->Delete(property);
    host->Delete(property); // TODO: For backwards compatibility.
    if (!success) {
        success = ctx->proxy_global_->Delete(property);
    }
    return scope.Close(Boolean::New(success));
  }

  static Handle<Array> GlobalPropertyEnumerator(const AccessorInfo &access_info) {
    HandleScope scope;
    Local<Object> host = access_info.Data()->ToObject();
    WrappedContext *ctx = ObjectWrap::Unwrap<WrappedContext>(host);
    if (ctx == NULL) return scope.Close(Handle<Array>());
    return scope.Close(ctx->sandbox_->GetPropertyNames());
  }
};


Persistent<FunctionTemplate> WrappedContext::constructor_template;


class WrappedScript : ObjectWrap {
 public:
  static void Initialize(Handle<Object> target);

  enum EvalInputFlags { compileCode, unwrapExternal };
  enum EvalContextFlags { thisContext, newContext, userContext };
  enum EvalOutputFlags { returnResult, wrapExternal };

  template <EvalInputFlags input_flag,
            EvalContextFlags context_flag,
            EvalOutputFlags output_flag>
  static Handle<Value> EvalMachine(const Arguments& args);

 protected:
  static Persistent<FunctionTemplate> constructor_template;

  WrappedScript() : ObjectWrap() {}
  ~WrappedScript();

  static Handle<Value> New(const Arguments& args);
  static Handle<Value> CreateContext(const Arguments& arg);
  static Handle<Value> RunInContext(const Arguments& args);
  static Handle<Value> RunInThisContext(const Arguments& args);
  static Handle<Value> RunInNewContext(const Arguments& args);
  static Handle<Value> CompileRunInContext(const Arguments& args);
  static Handle<Value> CompileRunInThisContext(const Arguments& args);
  static Handle<Value> CompileRunInNewContext(const Arguments& args);

  Persistent<Script> script_;
};

Persistent<Function> cloneObjectMethod;

void CloneObject(Handle<Object> recv,
                 Handle<Value> source, Handle<Value> target) {
  HandleScope scope;

  Handle<Value> args[] = {source, target};

  // Init
  if (cloneObjectMethod.IsEmpty()) {
    Local<Function> cloneObjectMethod_ = Local<Function>::Cast(
      Script::Compile(String::New(
        "(function(source, target) {\n\
           Object.getOwnPropertyNames(source).forEach(function(key) {\n\
           try {\n\
             var desc = Object.getOwnPropertyDescriptor(source, key);\n\
             if (desc.value === source) desc.value = target;\n\
             Object.defineProperty(target, key, desc);\n\
          } catch (e) {\n\
            // Catch sealed properties errors\n\
          }\n\
         });\n\
        })"
      ), String::New("binding:script"))->Run()
    );
    cloneObjectMethod = Persistent<Function>::New(cloneObjectMethod_);
  }

  cloneObjectMethod->Call(recv, 2, args);
}


void WrappedContext::Initialize(Handle<Object> target) {
  HandleScope scope;

  Local<FunctionTemplate> t = FunctionTemplate::New(WrappedContext::New);
  constructor_template = Persistent<FunctionTemplate>::New(t);
  constructor_template->InstanceTemplate()->SetInternalFieldCount(1);
  constructor_template->SetClassName(String::NewSymbol("Context"));

  target->Set(String::NewSymbol("Context"),
              constructor_template->GetFunction());
}


bool WrappedContext::InstanceOf(Handle<Value> value) {
  return !value.IsEmpty() && constructor_template->HasInstance(value);
}


Handle<Value> WrappedContext::New(const Arguments& args) {
  HandleScope scope;
 //printf("In WrappedContext::New\n");
  // TODO: change to correct exception types.
  if (args.Length() < 1) {
      Local<String> msg = String::New("Wrong number of arguments passed to WrappedContext constructor");
      return ThrowException(Exception::Error(msg));
  }
  if (!args[0]->IsObject()) {
      Local<String> msg = String::New("Argument to WrappedContext constructor must be an object.");
      return ThrowException(Exception::Error(msg));
  }
  Local<Object> sandbox = args[0]->ToObject();
  WrappedContext *ctx = new WrappedContext(sandbox, args.This());
  ctx->Wrap(args.This());
  CloneObject(args.This(), sandbox, args.This()); // TODO: For backwards compatability.
  return args.This();
}


// TODO: should these be handle, not local?
WrappedContext::WrappedContext(Local<Object> sandbox, Local<Object> host) : ObjectWrap() {
  sandbox_ = Persistent<Object>::New(sandbox);
  host_ = Persistent<Object>::New(host);
  host_.MakeWeak(NULL, HostObjectWeakCallback);
  context_ = CreateV8Context();
  proxy_global_ = Persistent<Object>::New(context_->Global());
}


WrappedContext::~WrappedContext() {
  printf("~WrappedContext\n");
  context_.Dispose();
  context_.Clear();
  proxy_global_.Dispose();
  proxy_global_.Clear();
  sandbox_.Dispose();
  sandbox_.Clear();
}


Local<Object> WrappedContext::NewInstance(const Arguments& args) {
  HandleScope scope;
  Handle<Value> argv[1];
  if (args.Length() > 0 && args[0]->IsObject()) {
    argv[0] = args[0];
  } else {
    argv[0] = Object::New();
  }
  Local<Object> context = constructor_template->GetFunction()->NewInstance(1, argv);
  return scope.Close(context);
}


Persistent<Context> WrappedContext::GetV8Context() {
  return context_;
}


Persistent<FunctionTemplate> WrappedScript::constructor_template;


void WrappedScript::Initialize(Handle<Object> target) {
  HandleScope scope;

  Local<FunctionTemplate> t = FunctionTemplate::New(WrappedScript::New);
  constructor_template = Persistent<FunctionTemplate>::New(t);
  constructor_template->InstanceTemplate()->SetInternalFieldCount(1);
  // Note: We use 'NodeScript' instead of 'Script' so that we do not
  // conflict with V8's Script class defined in v8/src/messages.js
  // See GH-203 https://github.com/joyent/node/issues/203
  constructor_template->SetClassName(String::NewSymbol("NodeScript"));

  NODE_SET_PROTOTYPE_METHOD(constructor_template,
                            "createContext",
                            WrappedScript::CreateContext);

  NODE_SET_PROTOTYPE_METHOD(constructor_template,
                            "runInContext",
                            WrappedScript::RunInContext);

  NODE_SET_PROTOTYPE_METHOD(constructor_template,
                            "runInThisContext",
                            WrappedScript::RunInThisContext);

  NODE_SET_PROTOTYPE_METHOD(constructor_template,
                            "runInNewContext",
                            WrappedScript::RunInNewContext);

  NODE_SET_METHOD(constructor_template,
                  "createContext",
                  WrappedScript::CreateContext);

  NODE_SET_METHOD(constructor_template,
                  "runInContext",
                  WrappedScript::CompileRunInContext);

  NODE_SET_METHOD(constructor_template,
                  "runInThisContext",
                  WrappedScript::CompileRunInThisContext);

  NODE_SET_METHOD(constructor_template,
                  "runInNewContext",
                  WrappedScript::CompileRunInNewContext);

  target->Set(String::NewSymbol("NodeScript"),
              constructor_template->GetFunction());
}


Handle<Value> WrappedScript::New(const Arguments& args) {
  if (!args.IsConstructCall()) {
    return FromConstructorTemplate(constructor_template, args);
  }

  HandleScope scope;

  WrappedScript *t = new WrappedScript();
  t->Wrap(args.Holder());

  return
    WrappedScript::EvalMachine<compileCode, thisContext, wrapExternal>(args);
}


WrappedScript::~WrappedScript() {
  script_.Dispose();
}


Handle<Value> WrappedScript::CreateContext(const Arguments& args) {
  HandleScope scope;
 //printf("Entering WrappedScript::CreateContext\n");
  Local<Object> context = WrappedContext::NewInstance(args);
 //printf("Leaving WrappedScript::CreateContext\n");
  return scope.Close(context);
}


Handle<Value> WrappedScript::RunInContext(const Arguments& args) {
  return
    WrappedScript::EvalMachine<unwrapExternal, userContext, returnResult>(args);
}


Handle<Value> WrappedScript::RunInThisContext(const Arguments& args) {
  return
    WrappedScript::EvalMachine<unwrapExternal, thisContext, returnResult>(args);
}


Handle<Value> WrappedScript::RunInNewContext(const Arguments& args) {
  return
    WrappedScript::EvalMachine<unwrapExternal, newContext, returnResult>(args);
}


Handle<Value> WrappedScript::CompileRunInContext(const Arguments& args) {
  return
    WrappedScript::EvalMachine<compileCode, userContext, returnResult>(args);
}


Handle<Value> WrappedScript::CompileRunInThisContext(const Arguments& args) {
  return
    WrappedScript::EvalMachine<compileCode, thisContext, returnResult>(args);
}


Handle<Value> WrappedScript::CompileRunInNewContext(const Arguments& args) {
  return
    WrappedScript::EvalMachine<compileCode, newContext, returnResult>(args);
}


template <WrappedScript::EvalInputFlags input_flag,
          WrappedScript::EvalContextFlags context_flag,
          WrappedScript::EvalOutputFlags output_flag>
Handle<Value> WrappedScript::EvalMachine(const Arguments& args) {
  HandleScope scope;
 //printf("Entering EvalMachine\n");

  if (input_flag == compileCode && args.Length() < 1) {
    return ThrowException(Exception::TypeError(
          String::New("needs at least 'code' argument.")));
  }

  const int sandbox_index = input_flag == compileCode ? 1 : 0;
  if (context_flag == userContext
    && !WrappedContext::InstanceOf(args[sandbox_index]))
  {
    return ThrowException(Exception::TypeError(
          String::New("needs a 'context' argument.")));
  }


  Local<String> code;
  if (input_flag == compileCode) code = args[0]->ToString();

  Local<Object> sandbox;
  if (context_flag == newContext) {
    sandbox = args[sandbox_index]->IsObject() ? args[sandbox_index]->ToObject()
                                              : Object::New();
  } else if (context_flag == userContext) {
    sandbox = args[sandbox_index]->ToObject();
  }

  const int filename_index = sandbox_index +
                             (context_flag == thisContext? 0 : 1);
  Local<String> filename = args.Length() > filename_index
                           ? args[filename_index]->ToString()
                           : String::New("evalmachine.<anonymous>");

  const int display_error_index = args.Length() - 1;
  bool display_error = false;
  if (args.Length() > display_error_index &&
      args[display_error_index]->IsBoolean() &&
      args[display_error_index]->BooleanValue() == true) {
    display_error = true;
  }

  Persistent<Context> context;

  Local<Array> keys;
  if (context_flag == newContext) {
    // Create the new context
    context = Context::New();
  } else if (context_flag == userContext) {
   //printf("Using a user context\n");
    // Use the passed in context
    WrappedContext *nContext = ObjectWrap::Unwrap<WrappedContext>(sandbox);
    context = nContext->GetV8Context();
  }

  // New and user context share code. DRY it up.
  if (context_flag == userContext || context_flag == newContext) {
    // Enter the context
    context->Enter();
  }
  if (context_flag == newContext) {
    // Copy everything from the passed in sandbox (either the persistent
    // context for runInContext(), or the sandbox arg to runInNewContext()).
    CloneObject(args.This(), sandbox, context->Global()->GetPrototype());
  }

  // Catch errors
  TryCatch try_catch;

  Handle<Value> result;
  Handle<Script> script;

  if (input_flag == compileCode) {
    // well, here WrappedScript::New would suffice in all cases, but maybe
    // Compile has a little better performance where possible
    script = output_flag == returnResult ? Script::Compile(code, filename)
                                         : Script::New(code, filename);
    if (script.IsEmpty()) {
      // FIXME UGLY HACK TO DISPLAY SYNTAX ERRORS.
      if (display_error) DisplayExceptionLine(try_catch);

      // Hack because I can't get a proper stacktrace on SyntaxError
      return try_catch.ReThrow();
    }
  } else {
    WrappedScript *n_script = ObjectWrap::Unwrap<WrappedScript>(args.Holder());
    if (!n_script) {
      return ThrowException(Exception::Error(
            String::New("Must be called as a method of Script.")));
    } else if (n_script->script_.IsEmpty()) {
      return ThrowException(Exception::Error(
            String::New("'this' must be a result of previous "
                        "new Script(code) call.")));
    }

    script = n_script->script_;
  }


  if (output_flag == returnResult) {
    result = script->Run();
    if (result.IsEmpty()) {
     //printf("Error running script\n");
      if (context_flag == newContext) {
       //printf("Cleaning up NEW context\n");
        context->DetachGlobal();
        context->Exit();
        context.Dispose();
      } else if (context_flag == userContext) {
       //printf("Exiting USER context\n");
        context->Exit();
      }
     //printf("Returning\n");
      return try_catch.ReThrow();
    }
   //printf("Script ran successfully\n");
  } else {
    WrappedScript *n_script = ObjectWrap::Unwrap<WrappedScript>(args.Holder());
    if (!n_script) {
      return ThrowException(Exception::Error(
            String::New("Must be called as a method of Script.")));
    }
    n_script->script_ = Persistent<Script>::New(script);
    result = args.This();
  }

  if (context_flag == newContext) {
    //printf("After run, cleaning up NEW context\n");
    // success! copy changes back onto the sandbox object.
    CloneObject(args.This(), context->Global()->GetPrototype(), sandbox);
    context->DetachGlobal();
    context->Exit();
    context.Dispose();
  } else if (context_flag == userContext) {
    //printf("After run, cleaning up USER context\n");
    // Exit the passed in context.
    context->Exit();
  }

 //printf("Leaving EvalMachine\n");
  return result == args.This() ? result : scope.Close(result);
}


void InitEvals(Handle<Object> target) {
  HandleScope scope;

  WrappedContext::Initialize(target);
  WrappedScript::Initialize(target);
}


}  // namespace node


NODE_MODULE(node_evals, node::InitEvals)

