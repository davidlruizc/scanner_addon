#include "scanner_addon.h"

Napi::FunctionReference ScannerAddon::constructor;

Napi::Object ScannerAddon::Init(Napi::Env env, Napi::Object exports) {
    Napi::HandleScope scope(env);

    Napi::Function func = DefineClass(env, "Scanner", {
        InstanceMethod("initialize", &ScannerAddon::Initialize),
        InstanceMethod("scan", &ScannerAddon::Scan),
        InstanceMethod("cleanup", &ScannerAddon::Cleanup),
    });

    constructor = Napi::Persistent(func);
    constructor.SuppressDestruct();

    exports.Set("Scanner", func);
    return exports;
}

ScannerAddon::ScannerAddon(const Napi::CallbackInfo& info) 
    : Napi::ObjectWrap<ScannerAddon>(info) {
    scanner = std::make_unique<TwainScanner>();
}

Napi::Value ScannerAddon::Initialize(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    auto result = scanner->Initialize();
    
    auto response = Napi::Object::New(env);
    response.Set("success", Napi::Boolean::New(env, result.success));
    response.Set("message", Napi::String::New(env, result.message));
    response.Set("deviceCount", Napi::Number::New(env, result.deviceCount));
    
    return response;
}

Napi::Value ScannerAddon::Scan(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    bool showUI = true;
    if (info.Length() > 0 && info[0].IsBoolean()) {
        showUI = info[0].As<Napi::Boolean>().Value();
    }
    
    auto result = scanner->Scan(showUI);
    
    auto response = Napi::Object::New(env);
    response.Set("success", Napi::Boolean::New(env, result.success));
    if (result.success) {
        response.Set("base64Image", Napi::String::New(env, result.base64Image));
    } else {
        response.Set("errorMessage", Napi::String::New(env, result.errorMessage));
    }
    
    return response;
}

Napi::Value ScannerAddon::Cleanup(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    bool result = scanner->Cleanup();
    
    auto response = Napi::Object::New(env);
    response.Set("success", Napi::Boolean::New(env, result));
    
    return response;
}

// Init addon
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    return ScannerAddon::Init(env, exports);
}

NODE_API_MODULE(scanner, Init)