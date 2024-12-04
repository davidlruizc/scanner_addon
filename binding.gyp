{
  "targets": [{
    "target_name": "scanner",
    "sources": [
      "src/cpp/scanner.cpp",
      "src/cpp/scanner_addon.cpp"
    ],
    "include_dirs": [
      "<!@(node -p \"require('node-addon-api').include\")",
      "src/cpp/twain"
    ],
    "defines": [ 
      "NAPI_DISABLE_CPP_EXCEPTIONS",
      "UNICODE",
      "_UNICODE"
    ],
    "msvs_settings": {
      "VCCLCompilerTool": {
        "ExceptionHandling": 1,
        "AdditionalOptions": ["/EHsc"],
        "DebugInformationFormat": "None"
      },
      "VCLinkerTool": {
        "GenerateDebugInformation": "false"
      }
    },
    "conditions": [
      ["OS=='win'", {
        "defines": [
          "_WIN32",
          "WIN32"
        ],
        "libraries": [
          "kernel32.lib",
          "user32.lib",
          "gdi32.lib",
          "winspool.lib",
          "comdlg32.lib",
          "advapi32.lib",
          "shell32.lib",
          "ole32.lib",
          "oleaut32.lib",
          "uuid.lib"
        ]
      }]
    ]
  }]
}