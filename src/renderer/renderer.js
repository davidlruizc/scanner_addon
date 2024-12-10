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

    if (result.success && result.images && result.images.length > 0) {
      // Store all images
      currentImages = result.images;

      updateStatus(
        `Scan completed successfully - ${result.images.length} pages`,
        "success"
      );

      // Create a container for multiple images
      let previewHtml = '<div class="multi-page-preview">';
      result.images.forEach((base64Image, index) => {
        previewHtml += `
          <div class="page-preview">
            <h3>Page ${index + 1}</h3>
            <img src="data:image/png;base64,${base64Image}" alt="Scanned document page ${
          index + 1
        }">
          </div>
        `;
      });
      previewHtml += "</div>";

      preview.innerHTML = previewHtml;
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
  if (!currentImages || currentImages.length === 0) {
    updateStatus("No images to save", "error");
    return;
  }

  try {
    saveButton.disabled = true;
    saveButton.textContent = "Saving...";

    for (let i = 0; i < currentImages.length; i++) {
      updateStatus(`Saving image ${i + 1} of ${currentImages.length}...`);

      const saved = await window.electronAPI.saveImage(currentImages[i]);

      if (!saved) {
        updateStatus(`Failed to save image ${i + 1}`, "error");
        return;
      }
    }

    updateStatus(
      `Successfully saved ${currentImages.length} images`,
      "success"
    );
  } catch (error) {
    updateStatus(`Error saving images: ${error.message}`, "error");
  } finally {
    saveButton.disabled = false;
    saveButton.textContent = "Save Images";
  }
}

const style = document.createElement("style");
style.textContent = `
.multi-page-preview {
  display: flex;
  flex-wrap: wrap;
  gap: 20px;
  justify-content: center;
}

.page-preview {
  flex: 0 1 45%;
  min-width: 300px;
  text-align: center;
}

.page-preview img {
  max-width: 100%;
  height: auto;
  border: 1px solid #ddd;
  border-radius: 4px;
  padding: 8px;
}

.page-preview h3 {
  margin: 0 0 10px 0;
  color: #666;
}
`;
document.head.appendChild(style);

// Event listeners
initButton.addEventListener("click", initializeScanner);
scanButton.addEventListener("click", performScan);
saveButton.addEventListener("click", saveImage);

// Cleanup on window unload
window.addEventListener("unload", () => {
  window.scanner.cleanup().catch(console.error);
});
