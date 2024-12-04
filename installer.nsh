// installer.nsh (NSIS script)
!macro customInit
  ; Check for TWAIN_32.DLL
  IfFileExists "$SYSDIR\TWAIN_32.DLL" TwainExists 0
    MessageBox MB_OK|MB_ICONSTOP "TWAIN driver not found. Please install your scanner's TWAIN driver first."
    Abort
  TwainExists:
!macroend