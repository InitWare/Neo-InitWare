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

import { edgeTypes } from './scheduler.mjs'
import * as fs from './fs.mjs';
import * as path from './path.mjs'


let lookupPaths = [
	"/etc/systemd/system",
	"/usr/lib/systemd/system"
];

let dependencyToReverse = {
	"Before": "After",
	"Requires": "RequiredBy",
	"Wants": "WantedBy",
	"PartOf": "ConsistsOf",
	"BindsTo": "BoundBy",
	"Requisite": "RequisiteOf",
	"Triggers": "TriggeredBy",
	"Conflicts": "ConflictedBy",
	"Upholds": "UpheldBy",
	"PropagatesReloadTo": "ReloadPropagatedFrom",
	"PropagatesStopTo": "StopPropagatedFrom"
};

let dependencyToReverse2 = Object.keys(dependencyToReverse).
	reduce((map, key) => {
		map[dependencyToReverse[key]] = key;
		return map;
	}, {});

let dependencyToEdgeTypes = {
	"Before": edgeTypes.kBefore,
	"Requires": edgeTypes.kAddStart | edgeTypes.kStartOnStarted,
	"Wants": edgeTypes.kAddStartNonreq | edgeTypes.kTryStartOnStarted,
	"PartOf": 0,
	"BindsTo": edgeTypes.kAddStart | edgeTypes.kStartOnStarted,
	"Requisite": edgeTypes.kAddVerify,
	"Triggers": 0,
	"Conflicts": edgeTypes.kAddStop,
	"Upholds": edgeTypes.kAddStartNonreq | edgeTypes.kStartOnStarted,
	"PropagatesReloadTo": edgeTypes.kPropagatesReloadTo,
	"PropagatesStopTo": edgeTypes.kPropagatesStopTo,

	"After": edgeTypes.kAfter,
	"RequiredBy": edgeTypes.kPropagatesStopTo | edgeTypes.kPropagatesRestartTo,
	"WantedBy": 0,
	"ConsistsOf": edgeTypes.kPropagatesStopTo | edgeTypes.kPropagatesRestartTo,
	"BoundBy": edgeTypes.kStopOnStopped | edgeTypes.kPropagatesStopTo |
		edgeTypes.kPropagatesRestartTo,
	"RequisiteOf": edgeTypes.kPropagatesStopTo | edgeTypes.kPropagatesRestartTo,
	"TriggeredBy": 0,
	"ConflictedBy": edgeTypes.kAddStopNonreq,
	"UpheldBy": 0,
	"ReloadPropagatedFrom": 0,
	"StopPropagatedFrom": 0,

	"JoinsNamespaceOf": 0,
	"Following": 0,
	"OnSuccess": edgeTypes.kOnSuccess,
	"OnFailure": edgeTypes.kOnFailure,
};

/**
 * Return the inverse type of a dependency, or null if there is no inverse type.
 * @param {string} dep
 * @return {string}
 */
function reverseDep(dep) {
	let reverse = dependencyToReverse[dep];
	if (typeof reverse != "undefined")
		return reverse;

	reverse = dependencyToReverse2[dep];
	if (typeof reverse != "undefined")
		return reverse;

	return null;
}

/**
 * Is name a genuine unit name? If so, returns a comprehensive breakdown of it.
 * @param name {string}
 * @return {{
 * 	name: string,
 * 	isInstance: boolean,
 * 	isTemplate: boolean,
 * 	template: string,
 * 	type: string
 * }}
 */
function matchUnitName(name) {
	let match = name.match(/^([^@. ]+)(?:(@)([^@. ]*)?)?(?:\.([^@. ]+))?$/);

	if (match) {
		let isInstance = false, isTemplate = false;

		if (match[2] == '@') {
			if (match[3] == "")
				isTemplate = true;
			else
				isInstance = true;

		}

		if (typeof match[4] == "undefined")
			match[4] = "service";

		return {
			name: match[0],
			isInstance: isInstance,
			isTemplate: isTemplate,
			template: match[3],
			type: match[4]
		};
	}
	else
		return null;
}

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
	let idx = name.lastIndexOf('.');
	return idx != -1 ? name.slice(+ 1) : "service";
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

/**
 * Return an array consisting of each the subpath with each lookup path
 * prepended.
 * @param {string} subpath
 * @return {Array.<String>}
 */
function prependLookupPaths(subpath) {
	let paths = [];

	for (const path of lookupPaths)
		paths.push(path + "/" + subpath);

	return paths;
}

/**
 * Parses systemd-style INI text into a JS object.
 * @param txt {string} text to parse
 * @param obj {object} object to parse into
 * @return {object} obj
 */
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
			/* @ts-ignore discard comment */
			;
		else if (split = line.match(/^( \t\n)*$/))
			/* @ts-ignore discard empty line */
			;
		else
			throw SyntaxError("Failed to parse line <" + line + ">");
	}

	return obj;
}

export function readSystemdUnit(name) {
	let obj = {};
	let paths = [];
	let fragmentPath = "";

	console.log(lookupPaths)

	/* first: locate the fragment file, and get its symlinked names */
	for (const path of lookupPaths) {
		try {
			paths = fs.getLinkedNames(path + "/" + name);
			break;
		} catch { }
	}

	if (paths == []) {
		console.log("Failed to find fragment for " + name + ".");
		return null;
	}

	fragmentPath = paths[0];

	let dat = fs.readFileSync(fragmentPath);
	let txt = String.fromCharCode.apply(null, new Uint8Array(dat));

	decode(txt, obj);

	let aliases = paths.map(path.basename).map(canonicaliseUnitName);
	let dropinDirPrefixes = aliases.flatMap(getDropinSubpaths).
		flatMap(prependLookupPaths);
	let dropinDeps = { "wants": "Wants", "requires": "Requires" };

	for (const depPath of Object.keys(dropinDeps)) {
		for (const path of dropinDirPrefixes) {
			try {
				let deps = fs.readdirSync(path + "." + depPath);
				let objects = deps.filter(matchUnitName);
				let depType = dropinDeps[depPath];

				if (objects.length == 0)
					continue;

				if (typeof obj["Unit"][depType] == "undefined")
					obj["Unit"][depType] = [];

				obj["Unit"][depType].push(objects);
			}
			catch (error) {
				/* maybe return if errno other than ENOENT? */
			}
		}
	}

	return obj;
}

export function loadSystemdUnit(name) {
	let obj = readSystemdUnit(name);
	/**
	 * Describes edges to originate from this node. Maps to-node to edge
	 * kind bitmask.
	 * @type {Object.<String, Number>}
	 */
	let edges_from = {};
	/**
	 * Describes edges to lead to this node. Maps from-node to edge kind
	 * bitmask.
	 * @type {Object.<String, Number>}
	 */
	let edges_to = {};

	if (obj == null)
		return -1;

	for (const depType of ["Before", "After", "Requires", "Wants",
		"PartOf", "BindsTo", "Requisite", "Upholds", "Conflicts",
		"PropagatesReloadTo", "ReloadPropagatedFrom"]) {
		if (typeof obj["Unit"][depType] == "undefined")
			continue;

		let depsForType = obj["Unit"][depType].flatMap(el => {
			/*
			 * Split up space-separated strings gotten from the
			 * fragment and .conf files.
			 */
			if (typeof el == "string")
				return el.split(/ /g);
			else return el;
		});

		depsForType.forEach((el) => {
			if (dependencyToEdgeTypes[depType] != 0)
				edges_from[el] |= dependencyToEdgeTypes[depType];
		});

		depsForType.forEach((el) => {
			let complement = reverseDep(depType);
			if (complement != null &&
				dependencyToEdgeTypes[complement] != 0)
				edges_to[el] = dependencyToEdgeTypes[complement];
		});
	}

	console.log("Edges from: " + JSON.stringify(edges_from));
	console.log("Edges to: " + JSON.stringify(edges_to));
}

console.log(JSON.stringify(loadSystemdUnit("default.target")));