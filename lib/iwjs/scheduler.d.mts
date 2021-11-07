/**
 * Bitmask of edge types.
 */
export enum edgeTypes {
	kAddStart,
	kAddStartNonreq,
	kAddVerify,
	kAddStop,
	kAddStopNonreq,
	kPropagatesStopTo,
	kPropagatesRestartTo,
	kPropagatesReloadTo,
	kStartOnStarted,
	kTryStartOnStarted,
	kStopOnStarted,
	kStopOnStopped,
	kOnSuccess,
	kOnFailure,
	kAfter,
	kBefore,
}

/**
 * Job types
 */
export enum jobTypes {

}

/** A job scheduler - there is only one in the system. */
export interface Scheduler {
	/**
	 * Load an object into the scheduler graph.
	 * @param aliases Set of names 
	 * @param edges_from Edges to be created from this node. Maps distal
	 * node names to edge mask.
	 * @param edges_to Edges to be created to this node. Maps proximal node
	 * names to edge mask.
	 */
	objectLoad(aliases: Array<string>, edges_from: { string: edgeTypes },
		edges_to: { string: edgeTypes }): void;

	/**
	 * Complete a job.
	 */
	jobComplete(jobID: number, state: jobTypes): void;
}

/** The global job scheduler. */
export const scheduler: Scheduler;