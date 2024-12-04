#pragma once
#include <string>
#include <memory>
#include <vector>
#include "twain/windows_wrapper.h"
#include "twain.h"

class ScannerResult {
public:
    bool success;
    std::string base64Image;
    std::string errorMessage;
    
    ScannerResult() : success(false) {}
};

class TwainScanner {
public:
    TwainScanner();
    ~TwainScanner();

    struct InitResult {
        bool success;
        std::string message;
        int deviceCount;
    };

    InitResult Initialize();
    ScannerResult Scan(bool showUI = true);
    bool Cleanup();

private:
    TW_IDENTITY m_AppId;
    TW_IDENTITY m_SrcId;
    DSMENTRYPROC m_pDSM;
    HMODULE m_hDSMLib;
    bool m_Initialized;
    std::string m_LastError;
    
    bool LoadDSM();
    void UnloadDSM();
    bool OpenDataSource();
    void CloseDataSource();
    bool NegotiateCapabilities();
    ScannerResult ProcessImage(TW_MEMREF handle);
    std::string ConvertToBase64(const std::vector<uint8_t>& data);
};