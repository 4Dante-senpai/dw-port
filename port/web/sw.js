/* Program-only cache. Disc, fixtures and user data never enter this cache. */
const VERSION = 'unstamped';
const CACHE = 'dw1-' + VERSION;
const FILES = ['/', '/dw1.js', '/dw1.wasm', '/io_worker.js', '/audio_worklet.js',
  '/dw1_pack.js', '/dw1_pack.wasm', '/us.tsv'];
self.addEventListener('install', event => event.waitUntil((async () => {
  try {
    const cache = await caches.open(CACHE);
    await cache.addAll(FILES.map(url => new Request(url, {cache:'reload'})));
    try {
      const version = await fetch(new Request('/version.txt', {cache:'reload'}));
      if (version.ok) await cache.put('/version.txt', version);
    } catch (_) { /* Optional on development hosts. */ }
  } catch (error) {
    await caches.delete(CACHE);
    throw error;
  }
})()));
self.addEventListener('activate', event => event.waitUntil((async () => {
  for (const name of await caches.keys())
    if (name.startsWith('dw1-') && name !== CACHE) await caches.delete(name);
  await self.clients.claim();
})()));
self.addEventListener('message', event => {
  if (event.data === 'activate') event.waitUntil(self.skipWaiting());
});
self.addEventListener('fetch', event => {
  const url = new URL(event.request.url);
  if (event.request.method !== 'GET' || url.origin !== self.location.origin) return;
  const navigation = event.request.mode === 'navigate' && ['/', '/index.html'].includes(url.pathname);
  // All stored keys are query-free. Other requests must reach the network untouched.
  if (!navigation && (url.search || ![...FILES, '/version.txt'].includes(url.pathname))) return;
  const key = navigation ? '/' : url.pathname;
  event.respondWith(caches.open(CACHE).then(async cache =>
    await cache.match(key) || fetch(event.request)));
});
