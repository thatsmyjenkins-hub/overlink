import { test, expect } from '@playwright/test';

test.describe('CYBERDECK web app', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/');
    await expect(page.locator('#live-badge')).toHaveText('LIVE', { timeout: 8000 });
  });

  test('deck control panel triggers command', async ({ page }) => {
    await expect(page.locator('#orch-actions .btn-orch')).toHaveCount(8);
    await page.locator('[data-action="pause"]').click();
    await expect(page.locator('#orch-status')).toContainText(/paused|Mock pause/i, { timeout: 5000 });
  });

  test('home shows applications and share tools', async ({ page }) => {
    await expect(page.locator('#app-launcher .app-tile')).toHaveCount(5);
    await expect(page.locator('#btn-copy-json')).toBeVisible();
    await expect(page.locator('#btn-download-report')).toBeVisible();
    await page.locator('.app-tile[data-view="netmap"]').click();
    await expect(page.locator('#view-netmap')).not.toHaveClass(/hidden/);
    await page.locator('nav.nav button[data-view="home"]').click();
    await expect(page.locator('#view-home')).not.toHaveClass(/hidden/);
  });

  test('share copy JSON updates status', async ({ page }) => {
    await page.locator('#btn-copy-json').click();
    await expect(page.locator('#share-status')).toContainText(/JSON copied|clipboard/i, {
      timeout: 5000,
    });
  });

  test('dashboard loads live stats and tables', async ({ page }) => {
    await expect(page.locator('[data-stat="hostCount"]')).toHaveText('3');
    await expect(page.locator('[data-stat="wifiCount"]')).toHaveText('5');
    await expect(page.locator('#dashboard-hosts-body tr')).toHaveCount(3);
    await expect(page.locator('#dashboard-wifi-body tr')).toHaveCount(5);
    await expect(page.locator('#event-log')).toContainText('CYBERDECK intel engine');
    await expect(page.locator('#scan-status')).toContainText('MONITOR');
  });

  test('navigation tabs render all views', async ({ page }) => {
    await page.locator('nav.nav button[data-view="netmap"]').click();
    await expect(page.locator('#view-netmap')).not.toHaveClass(/hidden/);
    await expect(page.locator('#host-list tr.clickable')).toHaveCount(3);
    await expect(page.locator('#netmap-status')).toBeVisible();

    await page.locator('nav.nav button[data-view="spectrum"]').click();
    await expect(page.locator('#view-spectrum')).not.toHaveClass(/hidden/);
    await expect(page.locator('#spectrum-body')).toContainText('ACCESS POINTS');
    await expect(page.locator('#spectrum-body tbody tr')).toHaveCount(5);

    await page.locator('nav.nav button[data-view="profiles"]').click();
    await expect(page.locator('#view-profiles')).not.toHaveClass(/hidden/);
    await expect(page.locator('#profiles-body tbody tr.clickable')).toHaveCount(3);

    await page.locator('nav.nav button[data-view="phantom"]').click();
    await expect(page.locator('#view-phantom')).not.toHaveClass(/hidden/);
    await expect(page.locator('#phantom-body')).toContainText('GuestOpen');
    await expect(page.locator('#phantom-body .demo-card')).toHaveCount(3);
  });

  test('host drill-down and back navigation', async ({ page }) => {
    await page.locator('#dashboard-hosts-body tr[data-ip="192.168.1.100"]').click();
    await expect(page.locator('#view-host')).not.toHaveClass(/hidden/);
    await expect(page.locator('#host-detail')).toContainText('192.168.1.100', { timeout: 5000 });
    await expect(page.locator('[data-host-action="load"]')).toBeVisible();
    await page.locator('[data-host-action="load"]').click();
    await expect(page.locator('#host-forensics')).toContainText('Dell', { timeout: 5000 });
    await expect(page.locator('#host-forensics table.cve-table tbody tr')).toHaveCount(2);
    await expect(page.locator('.port-chip')).toHaveCount(3);

    await page.locator('#nav-back').click();
    await expect(page.locator('#view-home')).not.toHaveClass(/hidden/);
  });

  test('CIRCL search button', async ({ page }) => {
    await page.locator('#dashboard-hosts-body tr[data-ip="192.168.1.100"]').click();
    await page.locator('[data-host-action="circl"]').click();
    await expect(page.locator('#circl-results')).toContainText('CVE-2019-0708', { timeout: 8000 });
  });

  test('help toggle shows and hides guides', async ({ page }) => {
    await page.locator('nav.nav button[data-view="spectrum"]').click();
    await page.locator('#toggle-guides').click();
    await expect(page.locator('#tab-guide .guide-panel')).toBeVisible();
    await expect(page.locator('#tab-guide')).toContainText('SPECTRUM');
    await page.locator('#toggle-guides').click();
    await expect(page.locator('#tab-guide')).toBeHidden();
  });

  test('alerts link to host detail', async ({ page }) => {
    const alert = page.locator('#dashboard-alerts .clickable[data-ip="192.168.1.100"]');
    await expect(alert.first()).toBeVisible();
    await alert.first().click();
    await expect(page.locator('#host-detail')).toContainText('192.168.1.100');
    await expect(page.locator('[data-host-action="forensics"]')).toBeVisible();
  });

  test('breadcrumb navigation', async ({ page }) => {
    await page.locator('nav.nav button[data-view="netmap"]').click();
    await expect(page.locator('#host-list tr[data-ip="192.168.1.1"]')).toBeVisible();
    await page.locator('#host-list tr[data-ip="192.168.1.1"]').click();
    await expect(page.locator('#breadcrumb')).toContainText('HOST');
    await page.locator('#breadcrumb a').first().click();
    await expect(page.locator('#view-netmap')).not.toHaveClass(/hidden/);
  });

  test('progress bar and scan metrics', async ({ page }) => {
    await expect(page.locator('#scan-progress')).toBeVisible();
    await expect(page.locator('#scan-status')).toContainText('Host hit rate');
    await expect(page.locator('#scan-status')).toContainText('Profiled');
    await expect(page.locator('#ops-bar')).toContainText('DECK');
  });

  test('Wi-Fi AP row opens AP tools', async ({ page }) => {
    await page.locator('nav.nav button[data-view="spectrum"]').click();
    await page.locator('#spectrum-body tbody tr').first().click();
    await expect(page.locator('#view-ap')).not.toHaveClass(/hidden/);
    await expect(page.locator('#ap-scenarios [data-ap-action="security_audit"]')).toBeVisible();
  });
});
