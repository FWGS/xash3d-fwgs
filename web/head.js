import * as fflate from 'https://cdn.skypack.dev/fflate@0.8.2?min';
var Module = typeof Module != 'undefined' ? Module : {};
Module.dynamicLibraries = [
	"filesystem_stdio",
	// "ref_gl.so",
	"ref_soft.so",
	"menu",
]
Module['INITIAL_MEMORY'] = 16777216 * 4
Module['canvas'] = document.getElementById('canvas')
