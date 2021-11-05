import * as initware from '@initware';
import * as fs from 'fs';
import { ServiceRestarter } from './restarters/service.mjs';
import { loadSystemdUnit } from './sdloader.mjs';

console.log(fs.getLinkedNames("/usr/local/etc/InitWare/system/default.target"))