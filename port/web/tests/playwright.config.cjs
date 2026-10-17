const { defineConfig } = require('/opt/node_modules/@playwright/test');
module.exports = defineConfig({
  testDir: __dirname,
  testMatch: (process.env.DW1_WEB_SUITE || 'boot') + '.spec.cjs',
  outputDir: process.env.DW1_WEB_BUILD + '/playwright-results-' + (process.env.DW1_WEB_SUITE || 'boot'),
  reporter: 'line',
  timeout: 60000,
  workers: 1,
  use: {baseURL: process.env.DW1_WEB_URL, screenshot: 'off', trace: 'off', video: 'off'},
  projects: [
    {name: 'chromium', use: {browserName: 'chromium',
      // The pinned headless shell does not expose OPFS on allowlisted HTTP origins.
      channel: process.env.DW1_WEB_INSECURE_ORIGIN ? 'chromium' : undefined, launchOptions: {args: ['--no-sandbox', '--enable-unsafe-swiftshader', ...(process.env.DW1_WEB_INSECURE_ORIGIN ?
      ['--unsafely-treat-insecure-origin-as-secure=' + new URL(process.env.DW1_WEB_INSECURE_ORIGIN).origin] : [])]}}},
    {name: 'firefox', use: {browserName: 'firefox', headless: false, launchOptions: {firefoxUserPrefs: {
      'webgl.force-enabled': true, 'gfx.webrender.software': true,
      'webgl.enable-webgl2': true
    }}}}
  ]
});
