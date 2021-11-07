export enum edgeTypes {

}

export enum jobTypes {

}

export interface Scheduler {
	/**
	 * Complete a job.
	 */
	completeJob(jobID: number, state: jobTypes): void;
}