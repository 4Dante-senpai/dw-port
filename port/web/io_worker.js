/* Only this dedicated worker owns the synchronous OPFS handle. */
let handle;
onmessage = async ({data}) => {
  if (data.type) {
    try {
      if (data.type === 'build' || data.type === 'manifest') await build(data);
      else {
        const root = await navigator.storage.getDirectory();
        if (data.type === 'clear') {
          await released(root);
          try { await root.removeEntry('dw1', {recursive: true}); }
          catch (e) { if (e.name !== 'NotFoundError') throw e; }
          postMessage({ready: true});
        } else if (data.type === 'status') {
          let sha256 = '';
          try {
            const dir = await root.getDirectoryHandle('dw1');
            const pack = await (await dir.getFileHandle('us.pack')).getFile();
            sha256 = (await (await (await dir.getFileHandle('us.pack.sha256')).getFile()).text()).trim();
            if (!pack.size || !/^[a-f0-9]{64}$/.test(sha256)) sha256 = '';
          } catch (e) { if (e.name !== 'NotFoundError') throw e; }
          postMessage({ready: true, sha256});
        } else throw Error('unknown worker request');
      }
    } catch (e) { postMessage({error: e.message}); }
    return;
  }
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

// WORKERFS reads File slices synchronously; the output node stores no Pack bytes in MEMFS.
async function build(data) {
  let output, dir;
  try {
    const files = data.files || [];
    const bins = files.filter(f => /\.bin$/i.test(f.name));
    const cues = files.filter(f => /\.cue$/i.test(f.name));
    // Only select an input here. The C parser owns cue, sector and Manifest validation.
    const image = cues.length === 1 ? cues[0] : bins.length === 1 ? bins[0] :
      files.length === 1 && /\.iso$/i.test(files[0].name) ? files[0] : null;
    if (!image || cues.length > 1 || bins.length !== 1 && !/\.iso$/i.test(image.name))
      throw Error('Seleccione un único .bin y, opcionalmente, su .cue');
    if (new Set(files.map(f => f.name)).size !== files.length) throw Error('Nombres de archivo duplicados');
    importScripts('/dw1_pack.js');
    let messages = [], printed = [];
    const module = await createPackModule({noInitialRun: true,
      print: text => printed.push(text), printErr: text => messages.push(text)});
    const {FS, WORKERFS} = module;
    FS.mkdir('/in'); FS.mkdir('/manifest'); FS.mkdir('/out');
    FS.mount(WORKERFS, {files}, '/in');
    let progress = 0, last = 0;
    const inputSize = bins[0]?.size || image.size;
    // Reserve half the bar for the Pack reread and the final unit for the commit.
    const total = 1000;
    postMessage({progress: 0, total});
    for (const file of files) {
      const node = FS.lookupPath('/in/' + file.name).node;
      const read = node.stream_ops.read;
      // Amortize FileReaderSync IPC without buffering an image. libc asks for small sectors.
      const cache = new Uint8Array(65536);
      let cacheAt = -1, cached = 0;
      node.stream_ops = {...node.stream_ops, read(stream, buffer, offset, length, position) {
        let copied = 0;
        while (copied < length) {
          const at = Math.floor(position / cache.length) * cache.length;
          if (at !== cacheAt) {
            cached = read(stream, cache, 0, cache.length, at); cacheAt = at;
            if (/\.bin$/i.test(file.name) || file === image && !bins.length) progress += cached;
            if (performance.now() - last > 100) {
              postMessage({progress: Math.min(progress / inputSize, 1) * 500, total}); last = performance.now();
            }
          }
          const start = position - cacheAt;
          const count = Math.min(length - copied, cached - start);
          if (count <= 0) break;
          buffer.set(cache.subarray(start, start + count), offset + copied);
          copied += count; position += count;
        }
        return copied;
      }};
    }

    const call = args => {
      messages = []; printed = [];
      const code = module.callMain(args);
      if (code) throw Error(messages.join('\n') || 'dw1-pack: exit ' + code);
    };
    if (data.type === 'manifest') {
      call(['manifest', '/in/' + image.name, '/manifest/us.tsv']);
      postMessage({ready: true, manifest: FS.readFile('/manifest/us.tsv', {encoding:'utf8'})});
      return;
    }
    FS.writeFile('/manifest/us.tsv', data.manifest);
    const root = await navigator.storage.getDirectory();
    await released(root);
    dir = await root.getDirectoryHandle('dw1', {create: true});
    const tmp = await dir.getFileHandle('us.pack.tmp', {create: true});
    output = await tmp.createSyncAccessHandle();
    output.truncate(0);
    FS.writeFile('/out/us.pack.tmp', new Uint8Array());
    const node = FS.lookupPath('/out/us.pack.tmp').node;
    node.node_ops = {...node.node_ops, setattr(n, attr) {
      if (attr.size !== undefined) { output.truncate(attr.size); n.usedBytes = attr.size; }
    }};
    let hashing = false, hashed = 0, packSize = 0;
    node.stream_ops = {...node.stream_ops,
      write(stream, buffer, offset, length, position) {
        const count = output.write(buffer.subarray(offset, offset + length), {at: position});
        node.usedBytes = Math.max(node.usedBytes, position + count);
        return count;
      },
      read(stream, buffer, offset, length, position) {
        const count = output.read(buffer.subarray(offset, offset + length), {at: position});
        if (hashing) {
          hashed += count;
          if (performance.now() - last > 100) {
            postMessage({progress: 500 + Math.min(hashed / packSize, 1) * 499, total, phase:'sha256'});
            last = performance.now();
          }
        }
        return count;
      }
    };
    call(['build', '/in/' + image.name, '/manifest/us.tsv', '/out/us.pack']);
    hashing = true; packSize = output.getSize(); last = -Infinity;
    postMessage({progress: 500, total, phase:'sha256'});
    call(['sha256', '/out/us.pack']);
    postMessage({progress: 999, total});
    const sha256 = printed.find(text => /^[a-f0-9]{64}$/.test(text));
    if (!sha256) throw Error('dw1-pack: missing SHA-256');
    output.flush(); output.close(); output = null;
    await commitPack(dir, tmp, sha256);
    postMessage({progress: total, total});
    postMessage({ready: true, sha256});
  } finally {
    if (output) output.close();
    if (dir) {
      try { await dir.removeEntry('us.pack.tmp'); }
      catch (e) { if (e.name !== 'NotFoundError') throw e; }
    }
  }
}

// Keep the previous file until both new names have committed. No Pack bytes are copied.
async function commitPack(dir, tmp, sha256) {
  let previous, moved = false, replaced = false;
  try {
    try { previous = await dir.getFileHandle('us.pack'); }
    catch (e) { if (e.name !== 'NotFoundError') throw e; }
    if (previous) { await previous.move('us.pack.previous'); moved = true; }
    await tmp.move('us.pack'); replaced = true;
    const file = await dir.getFileHandle('us.pack.sha256.tmp', {create:true});
    const sidecar = await file.createSyncAccessHandle();
    try {
      const bytes = new TextEncoder().encode(sha256 + '\n');
      sidecar.truncate(0);
      if (sidecar.write(bytes) !== bytes.length) throw Error('short sidecar write');
      sidecar.flush();
    } finally { sidecar.close(); }
    await file.move('us.pack.sha256');
  } catch (e) {
    if (replaced) await dir.removeEntry('us.pack');
    if (moved) await previous.move('us.pack');
    throw e;
  } finally {
    try { await dir.removeEntry('us.pack.sha256.tmp'); }
    catch (e) { if (e.name !== 'NotFoundError') throw e; }
  }
  if (moved) await dir.removeEntry('us.pack.previous');
}

// A page that ran the game may still be tearing down its reader worker, whose sync handle locks
// us.pack for a moment after navigation. Wait (bounded) until the Pack can be opened.
async function released(root) {
  let file;
  try { file = await (await root.getDirectoryHandle('dw1')).getFileHandle('us.pack'); }
  catch (e) { if (e.name === 'NotFoundError') return; throw e; }
  for (let i = 0; ; i++) {
    try { (await file.createSyncAccessHandle()).close(); return; }
    catch (e) {
      if (e.name !== 'NoModificationAllowedError' || i === 50) throw e;
      await new Promise(r => setTimeout(r, 100));
    }
  }
}
