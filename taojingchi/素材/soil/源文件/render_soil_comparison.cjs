// Render the approved HTML layout as two 1024x600 assets for USART HMI.
const fs = require("fs");
const path = require("path");
const { pathToFileURL } = require("url");
const { chromium } = require("playwright");

const soilRoot = path.resolve(__dirname, "..");
const htmlPath = path.join(soilRoot, "预览", "soil_comparison_preview.html");
const backgroundPath = path.join(soilRoot, "背景", "soil_compare_bg_1024x600.png");
const previewPath = path.join(soilRoot, "预览", "soil_compare_preview_1024x600.png");

async function main() {
  fs.mkdirSync(path.dirname(backgroundPath), { recursive: true });
  fs.mkdirSync(path.dirname(previewPath), { recursive: true });
  const browser = await chromium.launch({ channel: "msedge", headless: true });
  try {
    const page = await browser.newPage({ viewport: { width: 1200, height: 800 }, deviceScaleFactor: 1 });
    const baseUrl = pathToFileURL(htmlPath).href;
    await page.goto(baseUrl, { waitUntil: "load" });
    await page.locator(".screen").screenshot({ path: previewPath });
    await page.goto(`${baseUrl}?asset=background`, { waitUntil: "load" });
    await page.locator(".screen").screenshot({ path: backgroundPath });
  } finally {
    await browser.close();
  }
  process.stdout.write(`${backgroundPath}\n${previewPath}\n`);
}

main().catch((error) => {
  process.stderr.write(`${error.stack || error}\n`);
  process.exitCode = 1;
});
