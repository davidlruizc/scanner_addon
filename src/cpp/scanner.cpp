#include "scanner.h"
#include <Windows.h>
#include <vector>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <string>
#include <stdexcept>


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
    , m_DuplexSupported(false)
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

        // Get first available source
        rc = g_pDSM_Entry(&m_AppId, nullptr, DG_CONTROL, DAT_IDENTITY, MSG_GETFIRST, &m_SrcId);
        if (rc != TWRC_SUCCESS) {
            result.success = false;
            result.message = "No scanner found";
            return result;
        }

        // Open the data source
        rc = g_pDSM_Entry(&m_AppId, nullptr, DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, &m_SrcId);
        if (rc != TWRC_SUCCESS) {
            result.success = false;
            result.message = "Failed to open scanner";
            return result;
        }

        // Now check for duplex capability
        TW_CAPABILITY cap = {0};
        cap.Cap = CAP_DUPLEX;
        cap.ConType = TWON_ONEVALUE;
        
        rc = g_pDSM_Entry(&m_AppId, &m_SrcId, DG_CONTROL, DAT_CAPABILITY, MSG_GET, &cap);
        if (rc == TWRC_SUCCESS) {
            pTW_ONEVALUE pVal = (pTW_ONEVALUE)GlobalLock(cap.hContainer);
            if (pVal) {
                // Check if scanner supports any form of duplex (1-pass or 2-pass)
                m_DuplexSupported = (pVal->Item == TWDX_1PASSDUPLEX || pVal->Item == TWDX_2PASSDUPLEX);
                GlobalUnlock(cap.hContainer);
            }
            GlobalFree(cap.hContainer);
        }

        // Close the data source for now (we'll reopen it during scanning)
        rc = g_pDSM_Entry(&m_AppId, nullptr, DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &m_SrcId);

        // Count available devices
        TW_UINT32 sourceCount = 0;
        rc = g_pDSM_Entry(&m_AppId, nullptr, DG_CONTROL, DAT_IDENTITY, MSG_GETFIRST, &m_SrcId);
        while (rc == TWRC_SUCCESS) {
            sourceCount++;
            rc = g_pDSM_Entry(&m_AppId, nullptr, DG_CONTROL, DAT_IDENTITY, MSG_GETNEXT, &m_SrcId);
        }

        m_Initialized = true;
        result.success = true;
        result.message = "Initialized successfully";
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

    // Set resolution
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

    if (rc != TWRC_SUCCESS) {
        m_LastError = "Failed to set resolution";
        return false;
    }

    // Configure duplex if supported
    if (m_DuplexSupported) {
        // First, enable duplex capability
        cap.Cap = CAP_DUPLEXENABLED;
        cap.ConType = TWON_ONEVALUE;
        cap.hContainer = GlobalAlloc(GHND, sizeof(TW_ONEVALUE));
        if (!cap.hContainer) {
            m_LastError = "Failed to allocate memory for duplex setting";
            return false;
        }

        pVal = (pTW_ONEVALUE)GlobalLock(cap.hContainer);
        pVal->ItemType = TWTY_BOOL;
        pVal->Item = TRUE;
        GlobalUnlock(cap.hContainer);

        rc = g_pDSM_Entry(&m_AppId, &m_SrcId, DG_CONTROL, DAT_CAPABILITY, MSG_SET, (TW_MEMREF)&cap);
        GlobalFree(cap.hContainer);

        if (rc != TWRC_SUCCESS) {
            m_LastError = "Failed to enable duplex scanning";
            // Don't return false here - continue even if duplex setup fails
        }
    }

    return true;
}

ScannerResult TwainScanner::Scan(bool showUI) {
    ScannerResult result;
    m_LastError.clear();
    
    if (!m_Initialized) {
        result.errorMessage = "Scanner not initialized. Call Initialize() first.";
        return result;
    }

    HWND hwnd = NULL;
    const wchar_t CLASS_NAME[] = L"TwainWindowClass";
    bool windowClassRegistered = false;

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

        // Enable duplex if supported
        if (m_DuplexSupported) {
            TW_CAPABILITY cap;
            cap.Cap = CAP_DUPLEXENABLED;
            cap.ConType = TWON_ONEVALUE;
            cap.hContainer = GlobalAlloc(GHND, sizeof(TW_ONEVALUE));
            
            if (cap.hContainer) {
                pTW_ONEVALUE pVal = (pTW_ONEVALUE)GlobalLock(cap.hContainer);
                pVal->ItemType = TWTY_BOOL;
                pVal->Item = TRUE;
                GlobalUnlock(cap.hContainer);

                rc = g_pDSM_Entry(&m_AppId, &m_SrcId, DG_CONTROL, DAT_CAPABILITY, MSG_SET, (TW_MEMREF)&cap);
                GlobalFree(cap.hContainer);

                if (rc != TWRC_SUCCESS) {
                    printf("Warning: Failed to enable duplex scanning\n");
                }
            }
        }

        // Set up event handling window
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = CLASS_NAME;
        
        if (!RegisterClassExW(&wc)) {
            result.errorMessage = "Failed to register window class";
            CleanupSource();
            return result;
        }
        windowClassRegistered = true;
        
        hwnd = CreateWindowExW(
            0, CLASS_NAME, L"", WS_POPUP,
            0, 0, 1, 1, NULL, NULL,
            GetModuleHandleW(NULL), NULL
        );

        if (!hwnd) {
            result.errorMessage = "Failed to create message window";
            CleanupSource();
            if (windowClassRegistered) {
                UnregisterClassW(CLASS_NAME, GetModuleHandleW(NULL));
            }
            return result;
        }

        // Enable data source
        TW_USERINTERFACE ui = {0};
        ui.ShowUI = showUI ? TRUE : FALSE;
        ui.ModalUI = TRUE;
        ui.hParent = hwnd;

        rc = g_pDSM_Entry(&m_AppId, &m_SrcId, DG_CONTROL, DAT_USERINTERFACE, MSG_ENABLEDS, (TW_MEMREF)&ui);
        if (rc != TWRC_SUCCESS) {
            result.errorMessage = "Failed to enable scanner. Error: " + GetTwainErrorMessage(rc);
            CleanupResources(hwnd, windowClassRegistered);
            return result;
        }

        // Message loop for scanning
        bool scanning = true;
        std::vector<TW_HANDLE> imageHandles;
        DWORD startTime = GetTickCount();
        const DWORD SCAN_TIMEOUT = 300000; // 5 minutes timeout
        bool transferReady = false;

        while (scanning) {
            MSG msg;
            DWORD currentTime = GetTickCount();
            
            if (currentTime - startTime > SCAN_TIMEOUT) {
                result.errorMessage = "Scanning operation timed out";
                break;
            }

            if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                TW_EVENT twEvent;
                twEvent.pEvent = (TW_MEMREF)&msg;
                twEvent.TWMessage = MSG_NULL;

                rc = g_pDSM_Entry(&m_AppId, &m_SrcId, DG_CONTROL, DAT_EVENT, MSG_PROCESSEVENT, (TW_MEMREF)&twEvent);
                
                if (rc == TWRC_DSEVENT) {
                    printf("Processing TWAIN event: %d\n", twEvent.TWMessage);
                    
                    switch (twEvent.TWMessage) {
                        case MSG_XFERREADY:
                            transferReady = true;
                            printf("Transfer ready\n");
                            
                            while (transferReady) {
                                TW_IMAGEINFO imageInfo;
                                rc = g_pDSM_Entry(&m_AppId, &m_SrcId, DG_IMAGE, DAT_IMAGEINFO, MSG_GET, (TW_MEMREF)&imageInfo);
                                
                                if (rc == TWRC_SUCCESS) {
                                    TW_HANDLE handle = NULL;
                                    rc = g_pDSM_Entry(&m_AppId, &m_SrcId, DG_IMAGE, DAT_IMAGENATIVEXFER, MSG_GET, (TW_MEMREF)&handle);
                                    
                                    if (rc == TWRC_XFERDONE && handle) {
                                        printf("Image transferred successfully\n");
                                        imageHandles.push_back(handle);
                                    }
                                }

                                // Check for more pending transfers
                                TW_PENDINGXFERS pendingXfers = {0};
                                rc = g_pDSM_Entry(&m_AppId, &m_SrcId, DG_CONTROL, DAT_PENDINGXFERS, MSG_ENDXFER, (TW_MEMREF)&pendingXfers);
                                
                                if (pendingXfers.Count == 0) {
                                    printf("No more pending transfers\n");
                                    transferReady = false;
                                    scanning = false;
                                }
                            }
                            break;

                        case MSG_CLOSEDSREQ:
                            printf("Close DS requested\n");
                            scanning = false;
                            break;
                    }
                }

                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                startTime = GetTickCount();
            } else {
                Sleep(10);
            }
        }

        // Process scanned images
        if (!imageHandles.empty()) {
            printf("Processing %zu images\n", imageHandles.size());
            try {
                if (m_DuplexSupported && imageHandles.size() > 1) {
                    result = ProcessDuplexImages(imageHandles);
                } else {
                    result = ProcessImage(imageHandles[0]);
                }
            } catch (const std::exception& e) {
                result.errorMessage = std::string("Image processing failed: ") + e.what();
            }

            // Clean up handles regardless of processing result
            printf("Cleaning up image handles\n");
            for (auto handle : imageHandles) {
                if (handle) {
                    GlobalFree((HANDLE)handle);
                }
            }
            imageHandles.clear();
        }

        // Ensure UI is disabled before cleanup
        ui.ShowUI = FALSE;
        ui.ModalUI = TRUE;
        ui.hParent = hwnd;
        g_pDSM_Entry(&m_AppId, &m_SrcId, DG_CONTROL, DAT_USERINTERFACE, MSG_DISABLEDS, (TW_MEMREF)&ui);

        // Final cleanup
        CleanupResources(hwnd, windowClassRegistered);
        return result;
    }
    catch (const std::exception& e) {
        result.errorMessage = std::string("Scanning error: ") + e.what();
        CleanupResources(hwnd, windowClassRegistered);
        return result;
    }
}

void TwainScanner::CleanupSource() {
    g_pDSM_Entry(&m_AppId, nullptr, DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &m_SrcId);
}

void TwainScanner::CleanupResources(HWND hwnd, bool windowClassRegistered) {
    if (hwnd) {
        DestroyWindow(hwnd);
    }
    if (windowClassRegistered) {
        UnregisterClassW(L"TwainWindowClass", GetModuleHandleW(NULL));
    }
    CleanupSource();
}

bool TwainScanner::EnableDuplex() {
    TW_CAPABILITY cap;
    cap.Cap = CAP_DUPLEXENABLED;
    cap.ConType = TWON_ONEVALUE;
    cap.hContainer = GlobalAlloc(GHND, sizeof(TW_ONEVALUE));
    
    if (!cap.hContainer) {
        printf("Failed to allocate memory for duplex setting\n");
        return false;
    }

    pTW_ONEVALUE pVal = (pTW_ONEVALUE)GlobalLock(cap.hContainer);
    pVal->ItemType = TWTY_BOOL;
    pVal->Item = TRUE;
    GlobalUnlock(cap.hContainer);

    TW_UINT16 rc = g_pDSM_Entry(&m_AppId, &m_SrcId, DG_CONTROL, DAT_CAPABILITY, MSG_SET, (TW_MEMREF)&cap);
    GlobalFree(cap.hContainer);

    if (rc != TWRC_SUCCESS) {
        printf("Failed to enable duplex scanning\n");
        return false;
    }

    return true;
}

ScannerResult TwainScanner::ProcessDuplexImages(const std::vector<TW_HANDLE>& handles) {
    ScannerResult result;
    
    if (handles.empty()) {
        result.errorMessage = "No images to process";
        return result;
    }

    try {
        printf("Starting duplex image processing with %zu images\n", handles.size());

        // First pass: analyze all images and calculate total size needed
        std::vector<size_t> imageSizes;
        size_t maxWidth = 0;
        size_t maxHeight = 0;
        size_t totalHeight = 0;

        for (size_t i = 0; i < handles.size(); i++) {
            PBITMAPINFOHEADER pHeader = (PBITMAPINFOHEADER)GlobalLock((HANDLE)handles[i]);
            if (!pHeader) {
                throw std::runtime_error("Failed to lock image memory for analysis");
            }

            printf("Image %zu dimensions: %ldx%ld, bits per pixel: %d\n", 
                   i + 1, pHeader->biWidth, pHeader->biHeight, pHeader->biBitCount);

            // Store dimensions and handle negative heights
            size_t width = static_cast<size_t>(pHeader->biWidth);
            long height = pHeader->biHeight;
            size_t absHeight = height < 0 ? static_cast<size_t>(-height) : static_cast<size_t>(height);
            
            maxWidth = (width > maxWidth) ? width : maxWidth;
            maxHeight = (absHeight > maxHeight) ? absHeight : maxHeight;
            totalHeight += absHeight;

            // Calculate this image's row size and total size
            size_t rowSize = ((width * pHeader->biBitCount + 31) / 32) * 4;
            size_t imageSize = rowSize * absHeight;
            imageSizes.push_back(imageSize);

            GlobalUnlock((HANDLE)handles[i]);
        }

        printf("Max dimensions found: width=%zu, height=%zu\n", maxWidth, maxHeight);
        printf("Total combined height: %zu\n", totalHeight);

        // Calculate final buffer size
        size_t headerOffset = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        size_t maxRowSize = ((maxWidth * 24 + 31) / 32) * 4; // Using 24 bits per pixel
        size_t totalImageSize = maxRowSize * totalHeight;
        size_t totalFileSize = headerOffset + totalImageSize;

        printf("Calculated sizes:\n");
        printf("Header offset: %zu\n", headerOffset);
        printf("Max row size: %zu\n", maxRowSize);
        printf("Total image size: %zu\n", totalImageSize);
        printf("Total file size: %zu\n", totalFileSize);

        // Create output buffer
        std::vector<BYTE> combinedBuffer(totalFileSize);

        // Set up file header
        BITMAPFILEHEADER fileHeader = { 0 };
        fileHeader.bfType = 0x4D42; // "BM"
        fileHeader.bfSize = static_cast<DWORD>(totalFileSize);
        fileHeader.bfOffBits = static_cast<DWORD>(headerOffset);

        // Set up info header
        BITMAPINFOHEADER infoHeader = { 0 };
        infoHeader.biSize = sizeof(BITMAPINFOHEADER);
        infoHeader.biWidth = static_cast<LONG>(maxWidth);
        infoHeader.biHeight = static_cast<LONG>(totalHeight);
        infoHeader.biPlanes = 1;
        infoHeader.biBitCount = 24;
        infoHeader.biCompression = BI_RGB;
        infoHeader.biSizeImage = static_cast<DWORD>(totalImageSize);

        // Copy headers
        memcpy(combinedBuffer.data(), &fileHeader, sizeof(BITMAPFILEHEADER));
        memcpy(combinedBuffer.data() + sizeof(BITMAPFILEHEADER), &infoHeader, sizeof(BITMAPINFOHEADER));

        // Copy image data
        size_t currentOffset = headerOffset;
        size_t currentHeight = 0;

        for (size_t i = 0; i < handles.size(); i++) {
            printf("Processing image %zu of %zu\n", i + 1, handles.size());

            PBITMAPINFOHEADER pHeader = (PBITMAPINFOHEADER)GlobalLock((HANDLE)handles[i]);
            if (!pHeader) {
                throw std::runtime_error("Failed to lock image memory");
            }

            // Calculate source dimensions
            size_t srcWidth = static_cast<size_t>(pHeader->biWidth);
            long height = pHeader->biHeight;
            size_t srcHeight = height < 0 ? static_cast<size_t>(-height) : static_cast<size_t>(height);
            size_t srcRowSize = ((srcWidth * pHeader->biBitCount + 31) / 32) * 4;
            
            // Copy rows with potential padding
            BYTE* srcData = (BYTE*)pHeader + pHeader->biSize;
            for (size_t row = 0; row < srcHeight; row++) {
                size_t srcOffset = row * srcRowSize;
                size_t dstOffset = currentOffset + (row * maxRowSize);
                
                // Copy actual pixel data
                size_t pixelsToCopy = (srcWidth < maxWidth ? srcWidth : maxWidth) * 3;
                if (dstOffset + pixelsToCopy <= combinedBuffer.size()) {
                    memcpy(combinedBuffer.data() + dstOffset, srcData + srcOffset, pixelsToCopy);
                }
            }

            currentOffset += srcHeight * maxRowSize;
            currentHeight += srcHeight;
            GlobalUnlock((HANDLE)handles[i]);
        }

        // Convert to Base64
        static const char base64Chars[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string base64;
        base64.reserve(((combinedBuffer.size() + 2) / 3) * 4);

        for (size_t i = 0; i < combinedBuffer.size(); i += 3) {
            uint32_t b = (combinedBuffer[i] << 16) & 0xFF0000;
            if (i + 1 < combinedBuffer.size()) b |= (combinedBuffer[i + 1] << 8) & 0xFF00;
            if (i + 2 < combinedBuffer.size()) b |= combinedBuffer[i + 2] & 0xFF;

            base64.push_back(base64Chars[(b >> 18) & 0x3F]);
            base64.push_back(base64Chars[(b >> 12) & 0x3F]);
            base64.push_back(i + 1 < combinedBuffer.size() ? base64Chars[(b >> 6) & 0x3F] : '=');
            base64.push_back(i + 2 < combinedBuffer.size() ? base64Chars[b & 0x3F] : '=');
        }

        printf("Image processing completed successfully\n");
        result.success = true;
        result.base64Image = std::move(base64);

    } catch (const std::exception& e) {
        result.errorMessage = std::string("Duplex image processing error: ") + e.what();
        printf("Error during image processing: %s\n", result.errorMessage.c_str());
    }

    return result;
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