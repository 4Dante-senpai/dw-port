/* Only this dedicated worker owns the synchronous OPFS handle. */
let handle;
onmessage = async ({data}) => {
  try {
    const root = await navigator.storage.getDirectory();
    const dir = await root.getDirectoryHandle('dw1');
    const file = await dir.getFileHandle('us.pack');
    handle = await file.createSyncAccessHandle();
    const memory = new Uint8Array(data.memory);
    const words = new Int32Array(data.memory, data.address, 10);
    const size = handle.getSize();
    Atomics.store(words, 6, size >>> 0);
    Atomics.store(words, 7, Math.floor(size / 4294967296));
    postMessage({ready: true});
    for (;;) {
      while (Atomics.load(words, 0) !== 1) Atomics.wait(words, 0, 0);
      if (data.delay) Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, data.delay);
      try {
        const offset = (words[1] >>> 0) + (words[2] >>> 0) * 4294967296;
        const length = words[3] >>> 0, dest = words[4] >>> 0;
        if (!Number.isSafeInteger(offset) || offset > size || length > size - offset ||
            dest > memory.length || length > memory.length - dest) throw Error('range');
        const count = handle.read(memory.subarray(dest, dest + length), {at: offset});
        if (count !== length) throw Error('short read');
        Atomics.store(words, 5, 0);
      } catch (_) { Atomics.store(words, 5, 1); }
      Atomics.store(words, 0, 2);
      Atomics.notify(words, 0);
      // Sleep through the response state; the next request also notifies this address.
      while (Atomics.load(words, 0) === 2) Atomics.wait(words, 0, 2);
    }
  } catch (error) {
    if (handle) handle.close();
    postMessage({error: 'cannot open dw1/us.pack: ' + error.message});
  }
};
