import { readdir, readFile, writeFile } from 'node:fs/promises';
import { join, relative, sep } from 'node:path';
import { pathToFileURL } from 'node:url';
import { createHash } from 'node:crypto';

export function cacheableBuildFiles(paths) {
  return [...new Set(paths)].filter(path => !path.endsWith('.map') && !path.toLowerCase().endsWith('.wav') && !path.includes('debug-capture'));
}

export function buildIdForAssets(assets) {
  const hash = createHash('sha256');
  for (const asset of [...assets].sort((a, b) => a.path.localeCompare(b.path))) {
    hash.update(asset.path); hash.update('\0'); hash.update(asset.content); hash.update('\0');
  }
  return hash.digest('hex').slice(0, 12);
}

export function cacheNameFor(version, buildId) { return `sonic-link-${version}+${buildId}`; }
export const isObsoleteCache = (name, active) => name.startsWith('sonic-link-') && name !== active;

if (process.argv[1] && pathToFileURL(process.argv[1]).href === import.meta.url) {
  const root = new URL('../dist/', import.meta.url);
  async function walk(dir) {
    const entries = await readdir(dir, { withFileTypes: true });
    const files = [];
    for (const entry of entries) {
      const path = join(dir, entry.name);
      if (entry.isDirectory()) files.push(...await walk(path)); else files.push(path);
    }
    return files;
  }
  const files = cacheableBuildFiles((await walk(root.pathname)).map(path => `./${relative(root.pathname, path).split(sep).join('/')}`));
  const pkg = JSON.parse(await readFile(new URL('../package.json', import.meta.url), 'utf8'));
  const buildId = buildIdForAssets(await Promise.all(files.map(async path => ({ path, content: await readFile(new URL(path.slice(2), root)) }))));
  const version = `${pkg.version}+${buildId}`;
  await writeFile(new URL('../dist/version.json', import.meta.url), JSON.stringify({ version, packageVersion: pkg.version, buildId }));
  files.push('./version.json');
  const cacheVersion = cacheNameFor(pkg.version, buildId);
  const sw = `const CACHE=${JSON.stringify(cacheVersion)};\nconst PRECACHE=${JSON.stringify(files)};\nconst isObsoleteCache=${isObsoleteCache.toString()};\n` +
  `self.addEventListener('install',event=>event.waitUntil(caches.open(CACHE).then(cache=>cache.addAll(PRECACHE)).then(()=>self.skipWaiting())));\n` +
  `self.addEventListener('activate',event=>event.waitUntil(caches.keys().then(keys=>Promise.all(keys.filter(key=>isObsoleteCache(key,CACHE)).map(key=>caches.delete(key)))).then(()=>self.clients.claim())));\n` +
  `self.addEventListener('fetch',event=>{const request=event.request;const url=new URL(request.url);if(request.method!=='GET'||url.origin!==self.location.origin||url.pathname.endsWith('.wav'))return;event.respondWith(caches.match(request).then(hit=>hit||fetch(request).then(response=>{if(response.ok){const copy=response.clone();caches.open(CACHE).then(cache=>cache.put(request,copy));}return response;})));});\n`;
  await writeFile(new URL('../dist/sw.js', import.meta.url), sw);
  console.log(`PWA cache ${cacheVersion}: ${files.length} local assets.`);
}
