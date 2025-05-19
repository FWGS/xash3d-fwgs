async function mountZipToFS(zipArrayBuffer) {
	const zipData = new Uint8Array(zipArrayBuffer);
	const files = fflate.unzipSync(zipData);

	for (const [filename, content] of Object.entries(files)) {
		if (content.length === 0) continue; // likely a directory
		const path = '/xash/' + filename;
		const dir = path.split('/').slice(0, -1).join('/');
		await FS.mkdirTree(dir)
		await FS.writeFile(path, content);
	}
}

async function start() {
	await FS.mkdir('/rodir')
	await FS.mkdir('/xash')

	const res = await fetch('valve.zip')
	await mountZipToFS(await res.arrayBuffer())

	preInit();
	run();
}

start()
