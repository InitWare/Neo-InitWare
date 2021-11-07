/** Constants */
export namespace constants {
	const O_RDONLY: number;
	const O_WRONLY: number;
	const O_RDWR: number;
	const O_ACCMODE: number;

	const O_NONBLOCK: number;
	const O_APPEND: number;
}

/** Get a list of paths forming the symlink chain of the given path. */
export function getLinkedNames(path: string): Array<string>;

/**
 * Synchronously open a file.
 * @returns a file descriptor
 */
export function openSync(path: string, flags: number, mode: number): number;

/**
 * Synchronously retrieve a list of all files within a directory.
 */
export function readdirSync(path: string): Array<string>;

/**
 * Synchronously read data from a file descriptor.
 * @param fd 
 * @param buffer 
 * @param offset 
 * @param length 
 * @param position 
 * @returns number of bytes read
 */
export function readSync(fd: number, buffer: ArrayBuffer, offset: number,
	length: number, position: number);

/**
 * Synchronously read an entire file into a buffer or string.
 */
export function readFileSync(path: string, options): ArrayBuffer;