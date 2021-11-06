export function basename(str, ext) {
	let idx = str.lastIndexOf('/')

	if (idx == -1)
		return str;
	else if (idx == 0)
		return '/'
	else {
		let filename = str.slice(idx + 1);

		if (typeof ext != undefined && filename.endsWith(ext))
			return filename.slice(0, -(ext.length + 1));
		else
			return filename;
	}
}