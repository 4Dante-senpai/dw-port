const { defineConfig } = require('/opt/node_modules/@playwright/test');
module.exports = defineConfig({
  testDir: __dirname,
  testMatch: (process.env.DW1_WEB_SUITE || 'boot') + '.spec.cjs',
  outputDir: process.env.DW1_WEB_BUILD + '/playwright-results-' + (process.env.DW1_WEB_SUITE || 'boot'),
  reporter: 'line',
  timeout: 60000,
  workers: 1,
  use: {serviceWorkers: (process.env.DW1_WEB_SUITE || '').startsWith('offline') ? 'allow' : 'block', baseURL: process.env.DW1_WEB_URL, screenshot: 'off', trace: 'off', video: 'off'},
  projects: [
    {name: 'chromium', use: {browserName: 'chromium',
      // The pinned headless shell does not expose OPFS on allowlisted HTTP origins.
      channel: process.env.DW1_WEB_INSECURE_ORIGIN || /disc|cards|offline/.test(process.env.DW1_WEB_SUITE || '') ? 'chromium' : undefined, launchOptions: {args: ['--no-sandbox', '--explicitly-allowed-ports=9', '--autoplay-policy=document-user-activation-required', '--enable-unsafe-swiftshader', '--enable-blink-features=ForceEagerMeasureMemory', ...(process.env.DW1_WEB_INSECURE_ORIGIN ?
      ['--unsafely-treat-insecure-origin-as-secure=' + new URL(process.env.DW1_WEB_INSECURE_ORIGIN).origin] : [])]}}},
    {name: 'firefox', use: {browserName: 'firefox', headless: false, launchOptions: {firefoxUserPrefs: {
      // Let the negative network test reach the request layer on the prescribed port.
      'network.security.ports.banned.override': '9',
      'webgl.force-enabled': true, 'gfx.webrender.software': true,
      'webgl.enable-webgl2': true,
      // Web Audio needs a user gesture, as in a normal profile (FR-4, shell.spec.cjs).
      'media.autoplay.default': 1, 'media.autoplay.blocking_policy': 0, 'media.autoplay.block-webaudio': true
    }}}}
  ]
});
