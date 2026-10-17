onmessage = async ({data}) => {
  let handle;
  try {
    postMessage({step:'getDirectory'});
    const root = await navigator.storage.getDirectory();
    postMessage({step:'getDirectoryHandle'});
    const dir = await root.getDirectoryHandle('dw1', {create:true});
    postMessage({step:'getFileHandle'});
    // Invalidate the sidecar before truncating: a failed copy must never appear current.
    try { await dir.removeEntry('us.pack.sha256'); }
    catch (error) { if (error.name !== 'NotFoundError') throw error; }
    const file = await dir.getFileHandle('us.pack', {create:true});
    postMessage({step:'createSyncAccessHandle'});
    handle = await file.createSyncAccessHandle();
    handle.truncate(0);
    postMessage({step:'fetch'});
    const response = await fetch(typeof data === 'string' ? data : data.url);
    if (!response.ok) throw Error('fixture HTTP ' + response.status);
    postMessage({step:'stream'});
    const reader = response.body.getReader();
    let offset = 0;
    for (;;) {
      const {done, value} = await reader.read();
      if (done) break;
      if (handle.write(value, {at:offset}) !== value.length) throw Error('short write');
      offset += value.length;
      if (typeof data !== 'string') postMessage({progress:offset, total:Number(response.headers.get('Content-Length'))});
    }
    handle.flush(); handle.close(); handle = null;
    if (data.sha256) {
      const sidecar = await dir.getFileHandle('us.pack.sha256', {create:true});
      handle = await sidecar.createSyncAccessHandle();
      handle.truncate(0);
      const bytes = new TextEncoder().encode(data.sha256 + '\n');
      if (handle.write(bytes) !== bytes.length) throw Error('short sidecar write');
      handle.flush(); handle.close(); handle = null;
    }
    postMessage({size:offset});
  } catch (e) { if (handle) handle.close(); postMessage({error:e.message}); }
};
