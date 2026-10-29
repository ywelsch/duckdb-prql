// Runs the extension's SQL logic tests in DuckDB-Wasm inside headless Chrome.
//
// Usage:
//   node test/wasm/run.mjs --extension <prql.duckdb_extension.wasm> --duckdb-wasm <duckdb-wasm dist dir>
//                          [--flavor base|threads] [--chrome <path>] [--timeout <seconds>] [test files...]
//
// The test files default to all *.test files below test/sql. The DuckDB-Wasm dist directory is the `dist`
// folder of the @duckdb/duckdb-wasm npm package, which must be built from the same DuckDB version as the
// extension. Chrome is taken from --chrome, $CHROME_PATH, or a standard install location.
import { spawn } from 'node:child_process';
import { existsSync, mkdtempSync, readdirSync, readFileSync, rmSync, statSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { basename, delimiter, dirname, extname, join, relative, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';

const here = dirname(fileURLToPath(import.meta.url));
const testRoot = resolve(here, '..');

const { values: args, positionals } = parseArgs({
  allowPositionals: true,
  options: {
    extension: { type: 'string' },
    'duckdb-wasm': { type: 'string' },
    flavor: { type: 'string', default: 'base' },
    chrome: { type: 'string' },
    timeout: { type: 'string', default: '300' },
  },
});
if (!args.extension || !args['duckdb-wasm']) {
  console.error('usage: node test/wasm/run.mjs --extension <file> --duckdb-wasm <dist dir> [test files...]');
  process.exit(2);
}
const extensionFile = resolve(args.extension);
const duckdbWasmDir = resolve(args['duckdb-wasm']);
for (const path of [extensionFile, join(duckdbWasmDir, 'duckdb-browser.mjs')]) {
  if (!existsSync(path)) {
    console.error(`not found: ${path}`);
    process.exit(2);
  }
}

function listTests(dir) {
  return readdirSync(dir).sort().flatMap((name) => {
    const path = join(dir, name);
    if (statSync(path).isDirectory()) return listTests(path);
    return name.endsWith('.test') ? [path] : [];
  });
}
const testFiles = (positionals.length ? positionals.map((p) => resolve(p)) : listTests(join(testRoot, 'sql')))
  .map((path) => relative(testRoot, path).split(sep).join('/'));
if (testFiles.length === 0 || testFiles.some((path) => path.startsWith('..'))) {
  console.error(`test files must be below ${testRoot}`);
  process.exit(2);
}

// Static file server. Cross-origin isolation is required by the threads bundle and harmless for the others.
const routes = [
  ['/duckdb/', duckdbWasmDir],
  ['/test/', testRoot],
];
const contentTypes = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript',
  '.mjs': 'text/javascript',
  '.wasm': 'application/wasm',
  '.json': 'application/json',
};
const extensionPath = `/extension/${basename(extensionFile)}`;

function resolveRequest(pathname) {
  if (pathname === '/' || pathname === '/index.html') return join(here, 'index.html');
  if (pathname === extensionPath) return extensionFile;
  for (const [prefix, root] of routes) {
    if (!pathname.startsWith(prefix)) continue;
    const file = resolve(root, '.' + pathname.slice(prefix.length - 1));
    return file.startsWith(root + sep) ? file : null;
  }
  return null;
}

const server = createServer((req, res) => {
  const file = resolveRequest(decodeURIComponent(new URL(req.url, 'http://localhost').pathname));
  const headers = {
    'Cross-Origin-Opener-Policy': 'same-origin',
    'Cross-Origin-Embedder-Policy': 'require-corp',
    'Cache-Control': 'no-store',
  };
  if (!file || !existsSync(file) || !statSync(file).isFile()) {
    res.writeHead(404, headers).end();
    return;
  }
  headers['Content-Type'] = contentTypes[extname(file)] || 'text/plain; charset=utf-8';
  res.writeHead(200, headers).end(readFileSync(file));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const origin = `http://127.0.0.1:${server.address().port}`;

function findChrome() {
  const explicit = args.chrome || process.env.CHROME_PATH;
  if (explicit) return explicit;
  const candidates = {
    darwin: ['/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
      '/Applications/Chromium.app/Contents/MacOS/Chromium'],
    linux: ['google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser'],
    win32: [`${process.env.PROGRAMFILES}\\Google\\Chrome\\Application\\chrome.exe`],
  }[process.platform] || [];
  for (const candidate of candidates) {
    const paths = candidate.includes('/') || candidate.includes('\\') ? [candidate]
      : (process.env.PATH || '').split(delimiter).map((dir) => join(dir, candidate));
    const found = paths.find((path) => existsSync(path));
    if (found) return found;
  }
  console.error('Chrome not found, pass --chrome or set CHROME_PATH');
  process.exit(2);
}

// Talk to Chrome over the DevTools protocol on fds 3 (to Chrome) and 4 (from Chrome).
const profile = mkdtempSync(join(tmpdir(), 'prql-wasm-test-'));
const chrome = spawn(findChrome(), [
  '--headless=new', '--remote-debugging-pipe', `--user-data-dir=${profile}`,
  '--no-first-run', '--no-default-browser-check',
  // Unprivileged user namespaces are often unavailable to Chrome's sandbox on CI runners.
  ...(process.platform === 'linux' ? ['--no-sandbox'] : []),
  'about:blank',
], { stdio: ['ignore', 'ignore', 'pipe', 'pipe', 'pipe'] });
let chromeLog = '';
chrome.stderr.on('data', (d) => { chromeLog += d; });

let exitCode = null;
function finish(code, message) {
  if (exitCode !== null) return;
  exitCode = code;
  if (message) console.log(message);
  chrome.kill();
}
chrome.on('exit', (code, signal) => {
  if (exitCode === null) finish(1, `Chrome exited unexpectedly (${signal || code})\n${chromeLog}`);
  server.close();
  try {
    // Chrome's child processes may still be writing to the profile for a moment.
    rmSync(profile, { recursive: true, force: true, maxRetries: 10, retryDelay: 200 });
  } catch (e) {
    console.log(`Could not remove the Chrome profile ${profile}: ${e.message}`);
  }
  process.exit(exitCode);
});

let nextId = 1;
const pending = new Map();
const eventHandlers = [];
let buffered = '';
chrome.stdio[4].on('data', (chunk) => {
  buffered += chunk;
  let end;
  while ((end = buffered.indexOf('\0')) >= 0) {
    const msg = JSON.parse(buffered.slice(0, end));
    buffered = buffered.slice(end + 1);
    if (msg.id && pending.has(msg.id)) {
      const { resolve, reject } = pending.get(msg.id);
      pending.delete(msg.id);
      msg.error ? reject(new Error(`${msg.error.message} (${msg.error.code})`)) : resolve(msg.result);
    } else {
      eventHandlers.forEach((handler) => handler(msg));
    }
  }
});
const send = (method, params = {}, sessionId = undefined) => new Promise((resolve, reject) => {
  const id = nextId++;
  pending.set(id, { resolve, reject });
  chrome.stdio[3].write(JSON.stringify({ id, method, params, sessionId }) + '\0');
});

const { targetId } = await send('Target.createTarget', { url: 'about:blank' });
const { sessionId } = await send('Target.attachToTarget', { targetId, flatten: true });
eventHandlers.push((msg) => {
  if (msg.sessionId !== sessionId) return;
  if (msg.method === 'Runtime.bindingCalled' && msg.params.name === '__report') {
    const report = JSON.parse(msg.params.payload);
    if (report.log !== undefined) console.log(report.log);
    if (report.done) {
      const { passed, failed, skipped } = report.done;
      const summary = `\n${passed} passed, ${failed} failed, ${skipped} skipped`;
      finish(failed === 0 && passed > 0 ? 0 : 1, summary);
    }
  } else if (msg.method === 'Runtime.exceptionThrown') {
    const details = msg.params.exceptionDetails;
    console.log(`[page exception] ${details.exception?.description || details.text}`);
  } else if (msg.method === 'Log.entryAdded' && msg.params.entry.level === 'error') {
    console.log(`[page error] ${msg.params.entry.text} ${msg.params.entry.url || ''}`);
  }
});
await send('Runtime.enable', {}, sessionId);
await send('Runtime.addBinding', { name: '__report' }, sessionId);
await send('Log.enable', {}, sessionId);

const page = new URL('/index.html', origin);
page.searchParams.set('flavor', args.flavor);
page.searchParams.set('extension', extensionPath);
page.searchParams.set('tests', testFiles.map((path) => `/test/${path}`).join(','));
console.log(`Running ${testFiles.join(', ')} with ${basename(extensionFile)} (${args.flavor} bundle)`);
await send('Page.navigate', { url: page.href }, sessionId);

setTimeout(() => finish(1, `\nTimed out after ${args.timeout}s`), Number(args.timeout) * 1000);
