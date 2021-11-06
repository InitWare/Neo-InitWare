import * as initware from '@initware';

export const constants = initware.constants;
export const getLinkedNames = initware.getLinkedNames;

/* todo: handle string flags or mode */
export function openSync(path, flags, mode) {
	if (arguments.length == 1)
		flags = constants.O_RDONLY;
	if (arguments.length <= 2)
		mode = 0o666;
	return initware.openSync(path, flags, mode);
}

export const readdirSync = initware.readdirSync;

export function readSync(fd, buffer, offset, length, position) {
	if (arguments.length <= 3) {
		({ offset = 0, length = buffer.byteLength, position } = offset ? offset : {});
	}

	return initware.readSync(fd, buffer, offset, length, position);
}

export function readFileSync(path, options) {
	let fd = openSync(path);
	let buffer = new ArrayBuffer(8096);
	let nbytes = readSync(fd, buffer, 0, 8096, 0);

	return buffer.slice(0, nbytes);
}