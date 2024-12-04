# TWAIN Scanner Example

A cross-platform Electron application that demonstrates integration with TWAIN-compatible scanners using a native C++ addon.

## Overview

This project provides a desktop application that allows users to:

- Interface with TWAIN-compatible scanners
- Scan documents with or without UI
- Preview scanned images
- Save scanned documents as PNG files

## Prerequisites

- Node.js (v16 or higher)
- npm
- Windows operating system (TWAIN drivers are Windows-specific in this implementation)
- Visual Studio Build Tools with C++ support
- A TWAIN-compatible scanner with drivers installed

## Installation

1. Clone the repository
2. Install dependencies:

```bash
npm install
```

## Building

The project requires building both the native addon and the Electron application:

```bash
# Build everything
npm run build

# Build only the native addon
npm run build:addon

# Build only the Electron application
npm run build:electron
```

## Development

Start the application in development mode:

```bash
npm start
```

For debugging:

```bash
# Enable debug logging
set DEBUG=1
npm start
```

## Project Structure

```
├── src/
│   ├── cpp/               # Native C++ addon source
│   │   ├── scanner.cpp    # TWAIN implementation
│   │   └── scanner.h      # Scanner class definition
│   ├── renderer/          # Frontend UI
│   ├── main.js           # Electron main process
│   └── preload.js        # Preload script for IPC
├── binding.gyp           # Native addon build configuration
└── package.json
```

## Key Features

- TWAIN Data Source Manager (DSM) integration
- Native scanner access via C++ addon
- Automatic TWAIN driver detection
- Support for both UI and non-UI scanning modes
- Image format conversion and Base64 encoding
- Error handling and recovery
- Safe cleanup of TWAIN resources

## Technical Details

### Native Addon

The C++ addon provides the following functionality:

- TWAIN DSM initialization
- Scanner device enumeration
- Document scanning
- Image data transfer
- Memory management
- Error handling

### Electron Integration

The application uses:

- `node-addon-api` for C++ addon integration
- IPC for communication between main and renderer processes
- Context isolation for security
- Proper resource cleanup

## Building for Distribution

Create a distributable package:

```bash
npm run dist
```

This will create:

- A Windows installer (.exe)
- Portable executable
- Installation directory structure

## Troubleshooting

1. **Scanner Not Found**

   - Verify TWAIN driver installation
   - Check `C:\Windows\twain_32.dll` exists
   - Ensure scanner is connected and powered on

2. **Build Errors**

   - Verify Visual Studio Build Tools installation
   - Check `binding.gyp` configuration
   - Run `npm run clean` and rebuild

3. **Runtime Errors**
   - Check scanner connection
   - Verify TWAIN driver compatibility
   - Review application logs

## Known Limitations

- Windows-only support (current implementation)
- 32-bit TWAIN support only
- Single scanner selection
- PNG output format only

## Contributing

1. Fork the repository
2. Create a feature branch
3. Commit changes
4. Push to the branch
5. Create a Pull Request

## License

This project is licensed under the ISC License - see the LICENSE file for details.
