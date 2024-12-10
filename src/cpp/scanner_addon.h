#pragma once
#include <napi.h>
#include "scanner.h"

class ScannerAddon : public Napi::ObjectWrap<ScannerAddon> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    ScannerAddon(const Napi::CallbackInfo& info);

private:
    static Napi::FunctionReference constructor;
    
     Napi::Value Initialize(const Napi::CallbackInfo& info);
    Napi::Value Scan(const Napi::CallbackInfo& info);
    Napi::Value Cleanup(const Napi::CallbackInfo& info);
    Napi::Value IsDuplexSupported(const Napi::CallbackInfo& info);
    
    std::unique_ptr<TwainScanner> scanner;
};