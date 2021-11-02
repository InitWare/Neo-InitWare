import * as initware from '@initware';

export class ServiceRestarter {
	constructor() {
		this.iwrestarter = initware.createRestarter(this);
	}
}

let serviceRestarter = new ServiceRestarter();
initware.setRestarterForType(serviceRestarter.iwrestarter, "service");