//import { ServiceRestarter } from './restarters/service.mjs';
import * as restarter from './restarter.mjs'
import { loadSystemdUnit } from './sdloader.mjs';


class test extends restarter.Restarter {

}

/*import * as fs from './fs.mjs'

let dat = fs.readFileSync("/etc/passwd");
let txt = String.fromCharCode.apply(null, new Uint8Array(dat));
console.log("Read: " + txt);*/