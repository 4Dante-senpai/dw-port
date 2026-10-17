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
    {name: 'chromium', use: {browserName: 'chromium', launchOptions: {args: ['--no-sandbox', '--enable-unsafe-swiftshader']}}},
    {name: 'firefox', use: {browserName: 'firefox', headless: false, launchOptions: {firefoxUserPrefs: {
      'webgl.force-enabled': true, 'gfx.webrender.software': true,
      'webgl.enable-webgl2': true
    }}}}
  ]
});
