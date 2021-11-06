/*
 * SPDX-License-Identifier: BSD-4-Clause OR AGPL-3.0-only
 *
 * Copyright 2021 David MacKay. All rights reserved.
 */

/*
 * sdloader - systemd unit-file loader
 *
 * This module implements loading of legacy systemd unit files for Neo-InitWare.
 * Loading of a unit from its files by name proceeds thus:
 *   1. Each lookup path is searched for a filename matching that of the
 *   requested object. If no file is found, loading failed.
 *   2. The symlink path of the matching file is obtained. Every valid object
 *   name of this set is added to the set of aliases we consider.
 *   3. The scheduler is queried with each name in the set of aliases to find an
 *   object which matches. On finding such an object, any aliases in our set of
 *   aliases but missing from the alias set of the object are added.
 *   4. If an object was found and a reload was not requested, then that object
 *   is returned now.
 *   5. The matching file found in step 1 is called the `fragment' in systemd
 *   terminology. Its contents are read into a properties object. Now the
 *   drop-in directories are scanned. These are derived by convoluted means.
 */

import * as fs from './fs.mjs';
import * as path from './path.mjs'

let lookupPaths = [
	"/etc/systemd/system",
	"/usr/lib/systemd/system"
];

/** Canonicalise a unit name [i.e. add implicit .service if needed]. */
function canonicaliseUnitName(name) {
	if (name.lastIndexOf('.') == -1)
		return name + ".service";
	else
		return name;
}

function isInstance(name) {
	if (name.lastIndexOf('@') != -1)
		return true;
}

function unitType(name) {
	return name.slice(name.lastIndexOf('.') + 1);
}

function templateName(name) {
	let type = unitType(name);
	let idx = name.lastIndexOf('@');
	return name.substring(0, idx) + "." + type;
}

/**
 * Get the set of directory name elements to search for a name.
 * Todo: Break up units by '-', e.g. a-b-c.service, a-b-.service, a-.service.
 *  - Add a type.d too.
 */
function getDropinSubpaths(name, set) {
	let subpaths;

	if (typeof set != "undefined")
		set = [];

	set.push(name);

	if (isInstance(name))
		getDropinSubpaths(templateName(name), set);

	return set;
}

/** Return an array consisting of each the subpath with each lookup path prepended. */
function prependLookupPaths(subpath) {
	let paths = [];

	for (const path of lookupPaths)
		paths.push(path + "/" + subpath);

	return paths;
}

/** Parses systemd-style INI text into a JS object. */
function decode(txt, obj) {
	let cursect = obj; /** the current section to which properties go */
	let prop_cont = []; /** if set, current multi-line property [0]=[1] */

	function mergeProp(key, val) {
		if (typeof cursect[key] == "undefined")
			cursect[key] = [];
		cursect[key].push(val);
	}

	for (let line of txt.split(/[\r\n]+/g)) {
		let split = [];

		line = line.trim()

		if (prop_cont.length != 0) {
			if (line.endsWith('\\'))
				prop_cont[1] += line.slice(0, -1) + " ";
			else {
				mergeProp(prop_cont[0], prop_cont[1] + line);
				prop_cont = [];
			}
		}
		else if (split = line.match(/^\[(.*)\]$/)) {
			/* a section heading */
			obj[split[1]] = {};
			cursect = obj[split[1]];
		}
		else if (split = line.match(/^([^=]+)=(.*)$/)) {
			if (split[2].endsWith('\\')) {
				/* last char is '\'; multi-line property */
				prop_cont[0] = split[1];
				prop_cont[1] = split[2].slice(0, -1) + " ";
			}
			else
				/* single-line property */
				mergeProp(split[1], split[2]);
		}
		else if (split = line.match(/^([^=]+)=$/)) {
			/* empty assignments empty the value array */
			cursect[split[1]] = split[2];
		}
		else if (split = line.match(/^[#;].*$/))
			; /* discard comment */
		else if (split = line.match(/^( \t\n)*$/))
			; /* discard empty line */
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
ExecStart=/bin/sleep 5
sada`;
	let obj = {};
	let paths = [];
	let fragmentPath = "";

	console.log(lookupPaths)

	/* First, try to find the */
	for (const path of lookupPaths) {
		try {
			paths = fs.getLinkedNames(path + "/" + name);
			console.log("Found fragment at " + path);
			console.log("Symlink chain: " + paths);
			break;
		} catch { }
	}

	if (paths == []) {
		console.log("Failed to find fragment for " + name + ".");
		return -1;
	}

	fragmentPath = paths[0];
	console.log("Paths: " + paths);

	let dat = fs.readFileSync(fragmentPath);
	let txt = String.fromCharCode.apply(null, new Uint8Array(dat));
	console.log("Text: <<<" + txt + ">>>");

	decode(txt, obj);

	let aliases = paths.map(path.basename).map(canonicaliseUnitName);
	// todo: merge aliases into found object
	let dropinDirPrefixes = aliases.flatMap(getDropinSubpaths).flatMap(prependLookupPaths);

	for (const depType of ["wants", "requires "]) {
		for (const path of dropinDirPrefixes) {
			try {
				let deps = fs.readdirSync(path + "." + depType);
				console.log("Deps:  " + deps);
			}
			catch (error) {
				console.log("Error reading" + path + "." + depType + ": " + error);
			}
		}
	}

	return obj;
}

console.log(JSON.stringify(loadSystemdUnit("default.target")));