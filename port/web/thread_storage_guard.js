/* Test-only trap installed in every Emscripten pthread, including reader threads. */
if (!ENVIRONMENT_IS_PTHREAD) {
  Module.storageAccesses = 0;
  Module.storageAccessThreads = [];
} else if (new URL(self.location.href).searchParams.has('poison')) {
  Object.defineProperty(navigator, 'storage', {get() {
    _web_storage_guard_hit();
    throw Error('forbidden pthread navigator.storage access');
  }});
}
