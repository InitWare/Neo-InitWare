import { Scheduler } from "./scheduler.mjs";

export abstract class Restarter {
	constructor(scheduler: Scheduler);

	abstract startJob(jobID: number);
	abstract stopJob(jobID: number);
}