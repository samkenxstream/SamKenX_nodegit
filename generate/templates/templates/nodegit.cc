#include <node.h>
#include <v8.h>

#include <git2.h>
#include <map>
#include <algorithm>
#include <set>

#include <openssl/crypto.h>

#include "../include/init_ssh2.h"
#include "../include/lock_master.h"
#include "../include/nodegit.h"
#include "../include/wrapper.h"
#include "../include/promise_completion.h"
#include "../include/functions/copy.h"
{% each %}
  {% if type != "enum" %}
    #include "../include/{{ filename }}.h"
  {% endif %}
{% endeach %}
#include "../include/convenient_patch.h"
#include "../include/convenient_hunk.h"
#include "../include/filter_registry.h"

v8::Local<v8::Value> GetPrivate(v8::Local<v8::Object> object, v8::Local<v8::String> key) {
  v8::Local<v8::Value> value;
  Nan::Maybe<bool> result = Nan::HasPrivate(object, key);
  if (!(result.IsJust() && result.FromJust()))
    return v8::Local<v8::Value>();
  if (Nan::GetPrivate(object, key).ToLocal(&value))
    return value;
  return v8::Local<v8::Value>();
}

void SetPrivate(v8::Local<v8::Object> object, v8::Local<v8::String> key, v8::Local<v8::Value> value) {
  if (value.IsEmpty())
    return;
  Nan::SetPrivate(object, key, value);
}

void LockMasterEnable(const FunctionCallbackInfo<Value>& info) {
  LockMaster::Enable();
}

void LockMasterSetStatus(const FunctionCallbackInfo<Value>& info) {
  Nan::HandleScope scope;

  // convert the first argument to Status
  if(info.Length() >= 0 && info[0]->IsNumber()) {
    v8::Local<v8::Int32> value = Nan::To<v8::Int32>(info[0]).ToLocalChecked();
    LockMaster::Status status = static_cast<LockMaster::Status>(value->Value());
    if(status >= LockMaster::Disabled && status <= LockMaster::Enabled) {
      LockMaster::SetStatus(status);
      return;
    }
  }

  // argument error
  Nan::ThrowError("Argument must be one 0, 1 or 2");
}

void LockMasterGetStatus(const FunctionCallbackInfo<Value>& info) {
  info.GetReturnValue().Set(Nan::New(LockMaster::GetStatus()));
}

void LockMasterGetDiagnostics(const FunctionCallbackInfo<Value>& info) {
  LockMaster::Diagnostics diagnostics(LockMaster::GetDiagnostics());

  // return a plain JS object with properties
  v8::Local<v8::Object> result = Nan::New<v8::Object>();
  Nan::Set(result, Nan::New("storedMutexesCount").ToLocalChecked(), Nan::New(diagnostics.storedMutexesCount));
  info.GetReturnValue().Set(result);
}

static uv_mutex_t *opensslMutexes;

void OpenSSL_LockingCallback(int mode, int type, const char *, int) {
  if (mode & CRYPTO_LOCK) {
    uv_mutex_lock(&opensslMutexes[type]);
  } else {
    uv_mutex_unlock(&opensslMutexes[type]);
  }
}

void OpenSSL_IDCallback(CRYPTO_THREADID *id) {
  CRYPTO_THREADID_set_numeric(id, (unsigned long)uv_thread_self());
}

void OpenSSL_ThreadSetup() {
  opensslMutexes=(uv_mutex_t *)malloc(CRYPTO_num_locks() * sizeof(uv_mutex_t));

  for (int i=0; i<CRYPTO_num_locks(); i++) {
    uv_mutex_init(&opensslMutexes[i]);
  }

  CRYPTO_set_locking_callback(OpenSSL_LockingCallback);
  CRYPTO_THREADID_set_callback(OpenSSL_IDCallback);
}

ThreadPool libgit2ThreadPool(10, uv_default_loop());

extern "C" void init(v8::Local<v8::Object> target) {
  // Initialize thread safety in openssl and libssh2
  OpenSSL_ThreadSetup();
  init_ssh2();
  // Initialize libgit2.
  git_libgit2_init();

  Nan::HandleScope scope;

  Wrapper::InitializeComponent(target);
  PromiseCompletion::InitializeComponent();
  {% each %}
    {% if type != "enum" %}
      {{ cppClassName }}::InitializeComponent(target);
    {% endif %}
  {% endeach %}

  ConvenientHunk::InitializeComponent(target);
  ConvenientPatch::InitializeComponent(target);
  GitFilterRegistry::InitializeComponent(target);

  NODE_SET_METHOD(target, "enableThreadSafety", LockMasterEnable);
  NODE_SET_METHOD(target, "setThreadSafetyStatus", LockMasterSetStatus);
  NODE_SET_METHOD(target, "getThreadSafetyStatus", LockMasterGetStatus);
  NODE_SET_METHOD(target, "getThreadSafetyDiagnostics", LockMasterGetDiagnostics);

  v8::Local<v8::Object> threadSafety = Nan::New<v8::Object>();
  Nan::Set(threadSafety, Nan::New("DISABLED").ToLocalChecked(), Nan::New((int)LockMaster::Disabled));
  Nan::Set(threadSafety, Nan::New("ENABLED_FOR_ASYNC_ONLY").ToLocalChecked(), Nan::New((int)LockMaster::EnabledForAsyncOnly));
  Nan::Set(threadSafety, Nan::New("ENABLED").ToLocalChecked(), Nan::New((int)LockMaster::Enabled));

  Nan::Set(target, Nan::New("THREAD_SAFETY").ToLocalChecked(), threadSafety);

  LockMaster::Initialize();
}

NODE_MODULE(nodegit, init)
