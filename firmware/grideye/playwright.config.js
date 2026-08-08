import { defineConfig } from '@playwright/test';

const PORT = process.env.CYD_MOCK_PORT || 8765;
const BASE = `http://127.0.0.1:${PORT}`;

export default defineConfig({
  testDir: 'tests/e2e',
  timeout: 30000,
  retries: 0,
  use: {
    baseURL: BASE,
    viewport: { width: 390, height: 844 },
  },
  webServer: {
    command: `node scripts/mock_intel_server.mjs ${PORT}`,
    url: BASE,
    reuseExistingServer: !process.env.CI,
    timeout: 15000,
  },
});
