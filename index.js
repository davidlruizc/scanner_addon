const { Scanner } = require("./build/Release/scanner");

async function main() {
  const scanner = new Scanner();
  let scanTimeoutId = null;

  try {
    console.log("Initializing scanner...");
    const initResult = await scanner.initialize();
    console.log("Initialize result:", JSON.stringify(initResult, null, 2));

    if (initResult.success) {
      console.log("Starting scan...");

      // Create a promise that rejects after timeout
      const scanWithTimeout = Promise.race([
        scanner.scan(true),
        new Promise((_, reject) => {
          scanTimeoutId = setTimeout(() => {
            reject(new Error("Scan timeout after 60 seconds"));
          }, 60000);
        }),
      ]);

      const scanResult = await scanWithTimeout;
      clearTimeout(scanTimeoutId);

      if (scanResult.success && scanResult.base64Image) {
        const imageBuffer = Buffer.from(scanResult.base64Image, "base64");
        require("fs").writeFileSync("scanned_image.png", imageBuffer);
        console.log("Image saved as scanned_image.png");
      } else {
        console.error(
          "Scan failed:",
          scanResult.errorMessage || "Unknown error"
        );
      }
    } else {
      console.error("Scanner initialization failed:", initResult.message);
    }
  } catch (error) {
    clearTimeout(scanTimeoutId);
    console.error("Error:", error.message);
  } finally {
    console.log("Cleaning up...");
    try {
      await scanner.cleanup();
    } catch (cleanupError) {
      console.error("Cleanup error:", cleanupError);
    }
  }
}

main().catch(console.error);
