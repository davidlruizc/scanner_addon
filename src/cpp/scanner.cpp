#include "scanner.h"
#include <Windows.h>
#include <vector>
#include <algorithm>
#include <sstream>
#include <iomanip>

// Global TWAIN variables
HMODULE hTwainDLL = NULL;
DSMENTRYPROC g_pDSM_Entry = NULL;

std::string GetTwainErrorMessage(TW_UINT16 rc) {
    switch (rc) {
        case TWRC_SUCCESS: return "Success";
        case TWRC_FAILURE: return "Operation failed";
        case TWRC_CHECKSTATUS: return "Check status";
        case TWRC_CANCEL: return "User cancelled";
        case TWRC_DSEVENT: return "Device event";
        case TWRC_NOTDSEVENT: return "Not device event";
        case TWRC_XFERDONE: return "Transfer done";
        case TWRC_ENDOFLIST: return "End of list";
        default: return "Unknown error code: " + std::to_string(rc);
    }
}

bool LoadTwainLibrary() {
    if (hTwainDLL) return true;  // Already loaded
    
    // Try multiple possible paths for TWAIN_32.DLL
    const char* paths[] = {
        "C:\\Windows\\twain_32.dll",
        "C:\\Windows\\System32\\twain_32.dll",
        "C:\\Windows\\SysWOW64\\twain_32.dll",
        "twain_32.dll"  // Search in PATH
    };
    
    for (const char* path : paths) {
        hTwainDLL = LoadLibraryA(path);
        if (hTwainDLL) break;
    }

    if (!hTwainDLL) {
        DWORD error = GetLastError();
        char errorMsg[256];
        sprintf_s(errorMsg, "Failed to load twain_32.dll. Error code: %lu", error);
        return false;
    }
    
    g_pDSM_Entry = (DSMENTRYPROC)GetProcAddress(hTwainDLL, MAKEINTRESOURCEA(1));
    if (!g_pDSM_Entry) {
        g_pDSM_Entry = (DSMENTRYPROC)GetProcAddress(hTwainDLL, "DSM_Entry");
    }
    
    if (!g_pDSM_Entry) {
        DWORD error = GetLastError();
        FreeLibrary(hTwainDLL);
        hTwainDLL = NULL;
        return false;
    }

    return true;
}

void UnloadTwainLibrary() {
    if (hTwainDLL) {
        FreeLibrary(hTwainDLL);
        hTwainDLL = NULL;
        g_pDSM_Entry = NULL;
    }
}

TwainScanner::TwainScanner() 
    : m_Initialized(false)
    , m_hDSMLib(nullptr)
    , m_pDSM(nullptr)
{
    memset(&m_AppId, 0, sizeof(TW_IDENTITY));
    memset(&m_SrcId, 0, sizeof(TW_IDENTITY));
}

TwainScanner::~TwainScanner() {
    Cleanup();
    UnloadTwainLibrary();
}

TwainScanner::InitResult TwainScanner::Initialize() {
    InitResult result;
    
    if (m_Initialized) {
        result.success = true;
        result.message = "Already initialized";
        return result;
    }

    try {
        if (!LoadTwainLibrary()) {
            result.success = false;
            result.message = "Failed to load TWAIN_32.DLL";
            return result;
        }

        // Initialize TWAIN application identity
        m_AppId.Id = 1;
        m_AppId.Version.MajorNum = 2;
        m_AppId.Version.MinorNum = 4;
        m_AppId.Version.Language = TWLG_USA;
        m_AppId.Version.Country = TWCY_USA;
        strcpy_s(m_AppId.Version.Info, sizeof(m_AppId.Version.Info), "2.4");
        strcpy_s(m_AppId.ProductName, sizeof(m_AppId.ProductName), "Node TWAIN Scanner");
        strcpy_s(m_AppId.ProductFamily, sizeof(m_AppId.ProductFamily), "Node Scanner");
        strcpy_s(m_AppId.Manufacturer, sizeof(m_AppId.Manufacturer), "Your Company");
        m_AppId.SupportedGroups = DF_APP2 | DG_IMAGE | DG_CONTROL;
        m_AppId.ProtocolMajor = TWON_PROTOCOLMAJOR;
        m_AppId.ProtocolMinor = TWON_PROTOCOLMINOR;

        // Create a dummy window handle for the DSM
        HWND hwnd = CreateWindowA("STATIC", "TwainWindow", 
            WS_POPUP, 0, 0, 0, 0, HWND_DESKTOP, NULL, GetModuleHandle(NULL), NULL);
        
        if (!hwnd) {
            result.success = false;
            result.message = "Failed to create window handle";
            return result;
        }

        // Open Data Source Manager
        TW_UINT16 rc = g_pDSM_Entry(&m_AppId, nullptr, DG_CONTROL, DAT_PARENT, MSG_OPENDSM, (TW_MEMREF)&hwnd);
        if (rc != TWRC_SUCCESS) {
            DestroyWindow(hwnd);
            result.success = false;
            result.message = "Failed to open DSM";
            return result;
        }

        m_hDSMLib = (HMODULE)hwnd;
        m_Initialized = true;
        result.success = true;
        result.message = "Initialized successfully";
        
        // Count available devices
        TW_UINT32 sourceCount = 0;
        rc = g_pDSM_Entry(&m_AppId, nullptr, DG_CONTROL, DAT_IDENTITY, MSG_GETFIRST, &m_SrcId);
        while (rc == TWRC_SUCCESS) {
            sourceCount++;
            rc = g_pDSM_Entry(&m_AppId, nullptr, DG_CONTROL, DAT_IDENTITY, MSG_GETNEXT, &m_SrcId);
        }
        result.deviceCount = sourceCount;
        
        return result;
    }
    catch (const std::exception& e) {
        result.success = false;
        result.message = std::string("Initialization error: ") + e.what();
        return result;
    }
}

bool TwainScanner::NegotiateCapabilities() {
    TW_CAPABILITY cap;
    TW_UINT16 rc;
    
    // Set pixel type to RGB
    cap.Cap = ICAP_PIXELTYPE;
    cap.ConType = TWON_ONEVALUE;
    cap.hContainer = GlobalAlloc(GHND, sizeof(TW_ONEVALUE));
    if (!cap.hContainer) {
        m_LastError = "Failed to allocate memory for pixel type";
        return false;
    }

    pTW_ONEVALUE pVal = (pTW_ONEVALUE)GlobalLock(cap.hContainer);
    pVal->ItemType = TWTY_UINT16;
    pVal->Item = TWPT_RGB;
    GlobalUnlock(cap.hContainer);

    rc = g_pDSM_Entry(&m_AppId, &m_SrcId, DG_CONTROL, DAT_CAPABILITY, MSG_SET, (TW_MEMREF)&cap);
    GlobalFree(cap.hContainer);

    if (rc != TWRC_SUCCESS) {
        m_LastError = "Failed to set pixel type";
        return false;
    }

    // Set resolution (optional, but recommended)
    cap.Cap = ICAP_XRESOLUTION;
    cap.ConType = TWON_ONEVALUE;
    cap.hContainer = GlobalAlloc(GHND, sizeof(TW_ONEVALUE));
    if (!cap.hContainer) {
        m_LastError = "Failed to allocate memory for resolution";
        return false;
    }

    pVal = (pTW_ONEVALUE)GlobalLock(cap.hContainer);
    pVal->ItemType = TWTY_FIX32;
    TW_FIX32 resolution;
    resolution.Whole = 200;
    resolution.Frac = 0;
    memcpy(&pVal->Item, &resolution, sizeof(TW_FIX32));
    GlobalUnlock(cap.hContainer);

    rc = g_pDSM_Entry(&m_AppId, &m_SrcId, DG_CONTROL, DAT_CAPABILITY, MSG_SET, (TW_MEMREF)&cap);
    GlobalFree(cap.hContainer);

    return true;
}

ScannerResult TwainScanner::Scan(bool showUI) {
    ScannerResult result;
    m_LastError.clear();
    
    if (!m_Initialized) {
        result.errorMessage = "Scanner not initialized. Call Initialize() first.";
        return result;
    }

    try {
        // Find first available scanner
        TW_UINT16 rc = g_pDSM_Entry(&m_AppId, nullptr, DG_CONTROL, DAT_IDENTITY, MSG_GETFIRST, &m_SrcId);
        if (rc != TWRC_SUCCESS) {
            result.errorMessage = "No scanner found";
            return result;
        }

        // Open data source
        rc = g_pDSM_Entry(&m_AppId, nullptr, DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, &m_SrcId);
        if (rc != TWRC_SUCCESS) {
            result.errorMessage = "Failed to open scanner. Error: " + GetTwainErrorMessage(rc);
            return result;
        }

        // Set up event handling window
        const wchar_t CLASS_NAME[] = L"TwainWindowClass";
        
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = CLASS_NAME;
        
        if (!RegisterClassExW(&wc)) {
            result.errorMessage = "Failed to register window class";
            g_pDSM_Entry(&m_AppId, nullptr, DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &m_SrcId);
            return result;
        }
        
        HWND hwnd = CreateWindowExW(
            0,                          // Optional window styles
            CLASS_NAME,                 // Window class
            L"",                        // Window text
            WS_POPUP,                   // Window style
            0, 0, 1, 1,                // Size and position
            NULL,                       // Parent window    
            NULL,                       // Menu
            GetModuleHandleW(NULL),     // Instance handle
            NULL                        // Additional application data
        );

        if (!hwnd) {
            result.errorMessage = "Failed to create message window";
            UnregisterClassW(CLASS_NAME, GetModuleHandleW(NULL));
            g_pDSM_Entry(&m_AppId, nullptr, DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &m_SrcId);
            return result;
        }

        // Enable data source
        TW_USERINTERFACE ui;
        ui.ShowUI = showUI ? TRUE : FALSE;
        ui.ModalUI = TRUE;
        ui.hParent = hwnd;

        rc = g_pDSM_Entry(&m_AppId, &m_SrcId, DG_CONTROL, DAT_USERINTERFACE, MSG_ENABLEDS, (TW_MEMREF)&ui);
        if (rc != TWRC_SUCCESS) {
            result.errorMessage = "Failed to enable scanner. Error: " + GetTwainErrorMessage(rc);
            DestroyWindow(hwnd);
            UnregisterClassW(CLASS_NAME, GetModuleHandleW(NULL));
            g_pDSM_Entry(&m_AppId, nullptr, DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &m_SrcId);
            return result;
        }

        // Message loop for scanning
        bool scanning = true;
        MSG msg;
        TW_EVENT twEvent;
        twEvent.pEvent = (TW_MEMREF)&msg;
        
        while (scanning && GetMessage(&msg, NULL, 0, 0)) {
            // Let TWAIN process the message first
            twEvent.TWMessage = MSG_NULL;
            rc = g_pDSM_Entry(&m_AppId, &m_SrcId, DG_CONTROL, DAT_EVENT, MSG_PROCESSEVENT, (TW_MEMREF)&twEvent);
            
            if (rc == TWRC_DSEVENT) {
                switch (twEvent.TWMessage) {
                    case MSG_XFERREADY:
                        {
                            // Transfer the image
                            TW_IMAGEINFO imageInfo;
                            rc = g_pDSM_Entry(&m_AppId, &m_SrcId, DG_IMAGE, DAT_IMAGEINFO, MSG_GET, (TW_MEMREF)&imageInfo);
                            
                            if (rc == TWRC_SUCCESS) {
                                TW_HANDLE handle = NULL;
                                rc = g_pDSM_Entry(&m_AppId, &m_SrcId, DG_IMAGE, DAT_IMAGENATIVEXFER, MSG_GET, (TW_MEMREF)&handle);
                                
                                if (rc == TWRC_XFERDONE && handle) {
                                    result = ProcessImage(handle);
                                    scanning = false;
                                }
                            }
                            
                            // End transfer
                            TW_PENDINGXFERS pendingXfers = {};
                            g_pDSM_Entry(&m_AppId, &m_SrcId, DG_CONTROL, DAT_PENDINGXFERS, MSG_ENDXFER, (TW_MEMREF)&pendingXfers);
                            
                            if (pendingXfers.Count == 0) {
                                scanning = false;
                            }
                        }
                        break;

                    case MSG_CLOSEDSREQ:
                        scanning = false;
                        break;
                }
            }
            
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Cleanup
        g_pDSM_Entry(&m_AppId, &m_SrcId, DG_CONTROL, DAT_USERINTERFACE, MSG_DISABLEDS, (TW_MEMREF)&ui);
        g_pDSM_Entry(&m_AppId, nullptr, DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &m_SrcId);
        DestroyWindow(hwnd);
        UnregisterClassW(CLASS_NAME, GetModuleHandleW(NULL));

        if (!result.success && result.errorMessage.empty()) {
            result.errorMessage = "Scanning was cancelled or no image was acquired";
        }

        return result;
    }
    catch (const std::exception& e) {
        result.errorMessage = std::string("Scanning error: ") + e.what();
        return result;
    }
}

ScannerResult TwainScanner::ProcessImage(TW_MEMREF handle) {
    ScannerResult result;
    
    if (!handle) {
        result.errorMessage = "No image data received";
        return result;
    }

    try {
        PBITMAPINFOHEADER pHeader = (PBITMAPINFOHEADER)GlobalLock((HANDLE)handle);
        if (!pHeader) {
            result.errorMessage = "Failed to lock image memory";
            return result;
        }

        // Calculate sizes
        DWORD dwImageSize = pHeader->biSizeImage;
        DWORD dwFileSize = dwImageSize + sizeof(BITMAPFILEHEADER) + pHeader->biSize;
        
        // Create file header
        BITMAPFILEHEADER fileHeader = { 0 };
        fileHeader.bfType = 0x4D42; // "BM"
        fileHeader.bfSize = dwFileSize;
        fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + pHeader->biSize;

        // Create memory buffer for the complete BMP
        std::vector<BYTE> buffer(dwFileSize);
        
        // Copy file header
        memcpy(buffer.data(), &fileHeader, sizeof(BITMAPFILEHEADER));
        
        // Copy info header and color table
        DWORD headerSize = pHeader->biSize;
        memcpy(buffer.data() + sizeof(BITMAPFILEHEADER), pHeader, headerSize);
        
        // Copy image data
        memcpy(
            buffer.data() + sizeof(BITMAPFILEHEADER) + headerSize,
            (BYTE*)pHeader + headerSize,
            dwImageSize
        );

        GlobalUnlock((HANDLE)handle);
        GlobalFree((HANDLE)handle);

        // Convert to Base64
        static const char base64Chars[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string base64;
        base64.reserve(((buffer.size() + 2) / 3) * 4);

        for (size_t i = 0; i < buffer.size(); i += 3) {
            uint32_t b = (buffer[i] << 16) & 0xFF0000;
            if (i + 1 < buffer.size()) b |= (buffer[i + 1] << 8) & 0xFF00;
            if (i + 2 < buffer.size()) b |= buffer[i + 2] & 0xFF;

            base64.push_back(base64Chars[(b >> 18) & 0x3F]);
            base64.push_back(base64Chars[(b >> 12) & 0x3F]);
            base64.push_back(i + 1 < buffer.size() ? base64Chars[(b >> 6) & 0x3F] : '=');
            base64.push_back(i + 2 < buffer.size() ? base64Chars[b & 0x3F] : '=');
        }

        result.success = true;
        result.base64Image = base64;
        return result;
    }
    catch (const std::exception& e) {
        if (handle) {
            GlobalUnlock((HANDLE)handle);
            GlobalFree((HANDLE)handle);
        }
        result.errorMessage = std::string("Image processing error: ") + e.what();
        return result;
    }
}

bool TwainScanner::Cleanup() {
    if (!m_Initialized) {
        return true;
    }

    try {
        if (m_hDSMLib) {
            g_pDSM_Entry(&m_AppId, nullptr, DG_CONTROL, DAT_PARENT, MSG_CLOSEDSM, (TW_MEMREF)&m_hDSMLib);
            m_hDSMLib = nullptr;
        }

        m_Initialized = false;
        return true;
    }
    catch (const std::exception&) {
        return false;
    }
}