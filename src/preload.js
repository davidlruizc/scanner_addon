const { contextBridge, ipcRenderer } = require("electron");

// Load the scanner addon
let scannerInstance = null;

try {
  let Scanner;
  // Try multiple paths to load the addon
  try {
    Scanner = require("../build/Release/scanner.node").Scanner;
  } catch (e) {
    try {
      Scanner = require(process.resourcesPath + "/scanner.node").Scanner;
    } catch (e2) {
      Scanner = require("./build/Release/scanner.node").Scanner;
    }
  }
  console.log("Scanner module loaded");

  scannerInstance = new Scanner();
  console.log("Scanner instance created");
} catch (error) {
  console.error("Failed to initialize scanner:", error);
}

// Expose protected APIs to renderer
contextBridge.exposeInMainWorld("scanner", {
  initialize: () => {
    if (!scannerInstance) {
      return Promise.reject(new Error("Scanner not initialized"));
    }
    console.log("Calling initialize");
    return scannerInstance.initialize();
  },

  isDuplexSupported: () => {
    if (!scannerInstance) {
      return Promise.reject(new Error("Scanner not initialized"));
    }
    return scannerInstance.isDuplexSupported();
  },

  scan: (showUI = true) => {
    if (!scannerInstance) {
      return Promise.reject(new Error("Scanner not initialized"));
    }
    console.log("Calling scan");
    return scannerInstance.scan(showUI);
  },

  cleanup: () => {
    if (!scannerInstance) {
      return Promise.reject(new Error("Scanner not initialized"));
    }
    console.log("Calling cleanup");
    return scannerInstance.cleanup();
  },
});

contextBridge.exposeInMainWorld("electronAPI", {
  saveImage: (base64Data) => ipcRenderer.invoke("save-image", base64Data),
});

console.log("Preload script completed");
