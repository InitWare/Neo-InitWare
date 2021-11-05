let paths = [
	"/usr/lib/systemd/system",
	"/etc/systemd/system"
];

/** Parses systemd-style INI text into a JS object. */
function decode(txt) {
	let obj = {}; /** parsed object */
	let cursect = obj; /** the current section to which properties go */
	let prop_cont = []; /** if set, current multi-line property [0]=[1] */

	for (const line of txt.split(/[\r\n]+/g)) {
		let split = [];
		if (prop_cont.length != 0) {
			if (line.endsWith('\\'))
				prop_cont[1] += line.slice(0, -1) + " ";
			else {
				cursect[prop_cont[0]] = prop_cont[1] + line;
				prop_cont = [];
			}
		}
		else if (split = line.match(/\[(.*)\]/)) {
			/* a section heading */
			obj[split[1]] = {};
			cursect = obj[split[1]];
		}
		else if (split = line.match(/([^=]+)=(.*)/)) {
			if (split[2].endsWith('\\')) {
				/* last char is '\'; multi-line property */
				prop_cont[0] = split[1];
				prop_cont[1] = split[2].slice(0, -1) + " ";
			}
			else
				/* single-line property */
				cursect[split[1]] = split[2];
		}
		else if (split = line.match(/^[#;].*$/))
			; /* discard comment */
		else
			throw SyntaxError("Failed to parse line <" + line + ">");
	}

	return obj;
}

export function loadSystemdUnit(name) {
	let testserv = `# Comment 1
[Unit]
Description=A truly golden little service\\
designed for testing the SystemD unit-file loader.
Requires=b.target

[Service]
ExecStart=/bin/sleep 5`;
	let obj = {};

	for (const path in paths) {

	}

	return decode(testserv);
}

console.log(JSON.stringify(loadSystemdUnit("test.service")));