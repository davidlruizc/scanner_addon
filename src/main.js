const { app, BrowserWindow, ipcMain, dialog } = require("electron");
const path = require("path");
const fs = require("fs");

let mainWindow;

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1024,
    height: 768,
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      nodeIntegration: false,
      contextIsolation: true,
      sandbox: false, // Required for native addons
      webSecurity: true,
    },
  });

  mainWindow.loadFile(path.join(__dirname, "renderer", "index.html"));

  // Open DevTools in development
  if (process.env.DEBUG) {
    mainWindow.webContents.openDevTools();
  }

  mainWindow.webContents.on(
    "did-fail-load",
    (event, errorCode, errorDescription) => {
      console.error("Failed to load:", errorDescription);
    }
  );
}

function checkTwainDriver() {
  console.log("Checking for TWAIN driver...");

  const possiblePaths = [
    path.join(
      process.env.SystemRoot || "C:\\Windows",
      "SysWOW64",
      "TWAIN_32.DLL"
    ),
    path.join(process.env.windir || "C:\\Windows", "SysWOW64", "TWAIN_32.DLL"),
    path.join(
      process.env.SystemRoot || "C:\\Windows",
      "System32",
      "TWAIN_32.DLL"
    ),
    "C:\\Windows\\twain_32.dll",
  ];

  for (const twainPath of possiblePaths) {
    try {
      fs.accessSync(twainPath, fs.constants.F_OK);
      console.log(`TWAIN driver found at: ${twainPath}`);
      return true;
    } catch (err) {
      console.log(`Checking path: ${twainPath} - Not found`);
    }
  }

  return false;
}

function checkAddonExists() {
  const possiblePaths = [
    // Development path
    path.join(__dirname, "..", "build", "Release", "scanner.node"),
    // Production paths
    path.join(process.resourcesPath, "scanner.node"),
    path.join(app.getAppPath(), "build", "Release", "scanner.node"),
    path.join(__dirname, "build", "Release", "scanner.node"),
  ];
  try {
    for (const addonPath of possiblePaths) {
      try {
        console.log(`Checking path: ${addonPath}`);
        fs.accessSync(addonPath, fs.constants.F_OK);
        console.log(`Scanner addon found at: ${addonPath}`);
        global.scannerAddonPath = addonPath; // Store the working path
        return true;
      } catch (err) {
        console.log(`Not found at: ${addonPath}`);
      }
    }
  } catch (err) {
    console.error(`Scanner addon not found at: ${addonPath}`);
    return false;
  }
}

app.whenReady().then(() => {
  if (!checkTwainDriver()) {
    dialog.showErrorBox(
      "Missing Dependencies",
      "TWAIN driver not found. Please install your scanner's TWAIN driver first."
    );
    app.quit();
    return;
  }

  if (!checkAddonExists()) {
    dialog.showErrorBox(
      "Missing Dependencies",
      "Scanner addon not found. Please ensure the project is built correctly."
    );
    app.quit();
    return;
  }

  createWindow();

  // Debug: Show DevTools in development
  if (process.env.NODE_ENV === "development") {
    mainWindow.webContents.openDevTools();
  }
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") {
    app.quit();
  }
});

app.on("activate", () => {
  if (BrowserWindow.getAllWindows().length === 0) {
    createWindow();
  }
});

ipcMain.handle("save-image", async (event, base64Data) => {
  try {
    const { filePath, canceled } = await dialog.showSaveDialog({
      filters: [{ name: "Images", extensions: ["png"] }],
      defaultPath: path.join(app.getPath("documents"), "scanned_image.png"),
    });

    if (canceled || !filePath) {
      return false;
    }

    const buffer = Buffer.from(base64Data, "base64");
    fs.writeFileSync(filePath, buffer);
    return true;
  } catch (error) {
    console.error("Save image error:", error);
    dialog.showErrorBox("Save Error", `Failed to save image: ${error.message}`);
    return false;
  }
});

// Handle any uncaught errors
process.on("uncaughtException", (error) => {
  console.error("Uncaught Exception:", error);
  dialog.showErrorBox(
    "Error",
    `An unexpected error occurred: ${error.message}`
  );
});
