let currentImage = null;

// Get DOM elements
const initButton = document.getElementById("initButton");
const scanButton = document.getElementById("scanButton");
const saveButton = document.getElementById("saveButton");
const statusArea = document.getElementById("statusArea");
const duplexStatus = document.getElementById("duplexStatus");
const preview = document.getElementById("preview");

// Update status with optional class for styling
function updateStatus(message, type = "") {
  statusArea.textContent = message;
  statusArea.className = "status-box " + type;
}

// Initialize scanner
async function initializeScanner() {
  try {
    initButton.disabled = true;
    initButton.textContent = "Initializing...";
    updateStatus("Initializing scanner...");

    const result = await window.scanner.initialize();

    if (result.success) {
      // Check duplex support
      const isDuplexSupported = await window.scanner.isDuplexSupported();
      duplexStatus.textContent = isDuplexSupported
        ? "✓ Duplex Scanning Supported"
        : "Duplex Scanning Not Supported";
      duplexStatus.className =
        "status-box " + (isDuplexSupported ? "success" : "");

      updateStatus(
        `Scanner initialized successfully. Found ${result.deviceCount} device(s).`,
        "success"
      );
      scanButton.disabled = false;
      initButton.style.display = "none";
    } else {
      updateStatus(`Failed to initialize scanner: ${result.message}`, "error");
      initButton.disabled = false;
    }
  } catch (error) {
    updateStatus(`Error initializing scanner: ${error.message}`, "error");
    initButton.disabled = false;
  } finally {
    initButton.textContent = "Initialize Scanner";
  }
}

// Perform scan
async function performScan() {
  try {
    scanButton.disabled = true;
    saveButton.disabled = true;
    scanButton.textContent = "Scanning...";
    updateStatus("Scanning in progress...");
    preview.innerHTML = '<span class="loading">Scanning</span>';

    const result = await window.scanner.scan(true);

    if (result.success && result.base64Image) {
      currentImage = result.base64Image;
      updateStatus("Scan completed successfully", "success");
      preview.innerHTML = `<img src="data:image/png;base64,${result.base64Image}" alt="Scanned document">`;
      saveButton.disabled = false;
    } else {
      updateStatus(result.errorMessage || "Failed to scan", "error");
      preview.innerHTML = "<span>No image scanned yet</span>";
    }
  } catch (error) {
    updateStatus(`Scanning error: ${error.message}`, "error");
    preview.innerHTML = "<span>No image scanned yet</span>";
  } finally {
    scanButton.disabled = false;
    scanButton.textContent = "Scan Document";
  }
}

// Save scanned image
async function saveImage() {
  if (!currentImage) {
    updateStatus("No image to save", "error");
    return;
  }

  try {
    saveButton.disabled = true;
    saveButton.textContent = "Saving...";
    updateStatus("Saving image...");

    const saved = await window.electronAPI.saveImage(currentImage);

    if (saved) {
      updateStatus("Image saved successfully", "success");
    } else {
      updateStatus("Failed to save image", "error");
    }
  } catch (error) {
    updateStatus(`Error saving image: ${error.message}`, "error");
  } finally {
    saveButton.disabled = false;
    saveButton.textContent = "Save Image";
  }
}

// Event listeners
initButton.addEventListener("click", initializeScanner);
scanButton.addEventListener("click", performScan);
saveButton.addEventListener("click", saveImage);

// Cleanup on window unload
window.addEventListener("unload", () => {
  window.scanner.cleanup().catch(console.error);
});
