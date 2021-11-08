import { Scheduler } from "./scheduler.mjs";

/** The abstract superclass of delegated restarters. */
export abstract class Restarter {
	constructor(scheduler: Scheduler);

	abstract startJob(jobID: number);
	abstract stopJob(jobID: number);
}