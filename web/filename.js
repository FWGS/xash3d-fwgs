filename = PATH.normalize(filename);
const NAMES_MAP = {
	server: "server.wasm",
	client: "client.wasm",
};
filename = NAMES_MAP[filename] || filename;
