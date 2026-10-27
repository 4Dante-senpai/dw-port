// Reopen the committed OPFS file and hash it in bounded slices, independently of the writer.
onmessage = async () => {
  try {
    const dir = await (await navigator.storage.getDirectory()).getDirectoryHandle('dw1');
    const file = await (await dir.getFileHandle('us.pack')).getFile();
    const sidecar = (await (await (await dir.getFileHandle('us.pack.sha256')).getFile()).text()).trim();
    importScripts('/dw1_pack.js');
    const output = [], errors = [];
    const module = await createPackModule({noInitialRun:true,
      print:t => output.push(t), printErr:t => errors.push(t)});
    module.FS.mkdir('/in'); module.FS.mount(module.WORKERFS, {files:[file]}, '/in');
    const code = module.callMain(['sha256', '/in/us.pack']);
    if (code) throw Error(errors.join('\n'));
    postMessage({sha256:output.find(t => /^[a-f0-9]{64}$/.test(t)), sidecar});
  } catch (e) { postMessage({error:e.message}); }
};
