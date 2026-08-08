#!/usr/bin/env node
import { spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const NODE = process.env.CYD_NODE ||
  '/Applications/Cursor.app/Contents/Resources/app/resources/helpers/node';

function run(cmd, args) {
  return new Promise((resolve, reject) => {
    const p = spawn(cmd, args, { cwd: ROOT, stdio: 'inherit', env: { ...process.env, PATH: path.dirname(NODE) + ':' + process.env.PATH } });
    p.on('exit', (code) => (code === 0 ? resolve() : reject(new Error(`${cmd} exit ${code}`))));
  });
}

await run(NODE, ['--test', 'tests/api.contract.test.mjs']);
await run(path.join(ROOT, 'node_modules/.bin/playwright'), ['test']);
console.log('\n✓ All web tests passed\n');
