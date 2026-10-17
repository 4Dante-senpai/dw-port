onmessage = async ({data}) => {
  let handle;
  try {
    postMessage({step:'getDirectory'});
    const root = await navigator.storage.getDirectory();
    postMessage({step:'getDirectoryHandle'});
    const dir = await root.getDirectoryHandle('dw1', {create:true});
    postMessage({step:'getFileHandle'});
    const file = await dir.getFileHandle('us.pack', {create:true});
    postMessage({step:'createSyncAccessHandle'});
    handle = await file.createSyncAccessHandle();
    handle.truncate(0);
    postMessage({step:'fetch'});
    const response = await fetch(data);
    if (!response.ok) throw Error('fixture HTTP ' + response.status);
    postMessage({step:'stream'});
    const reader = response.body.getReader();
    let offset = 0;
    for (;;) {
      const {done, value} = await reader.read();
      if (done) break;
      if (handle.write(value, {at:offset}) !== value.length) throw Error('short write');
      offset += value.length;
    }
    handle.flush(); handle.close(); handle = null;
    postMessage({size:offset});
  } catch (e) { if (handle) handle.close(); postMessage({error:e.message}); }
};
