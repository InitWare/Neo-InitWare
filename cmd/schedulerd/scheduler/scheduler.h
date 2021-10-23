#ifndef SCHEDULER_H_
#define SCHEDULER_H_

#include <list>
#include <memory>
#include <queue>
#include <string>
//#include <unordered_set>
#define unordered_set list

class Job;
class Schedulable;

#define BITFLAG(n) (1 << n)

/** An edge between two entities in the Schedulable Objects Graph. */
class Edge {
    public:
	/**
	 * This enumerated type defines which relationships #from has with #to.
	 */
	enum Type {
		/**
		 * \defgroup Co-Enqueue Enqueue Other Jobs
		 * @{
		 */
		/**
		 * For a start job, enqueue a required dependency start job on
		 * #to.
		 */
		kRequire = BITFLAG(0),
		/**
		 * For a start job, enqueue a non-required dependency start job
		 * on #to, but ignore result.
		 */
		kWant = BITFLAG(1),
		/**
		 * For a start job, enqueue a required dependency verify (check
		 * if online) job on #to, and fail if that fails.
		 */
		kRequisite = BITFLAG(2),
		/**
		 * For a start job, enqueue a required dependency stop job on
		 * #to.
		 */
		kConflict = BITFLAG(3),
		/**
		 * For a start job, enqueue a non-required dependency stop job
		 * on #to.
		 */
		kConflictedBy = BITFLAG(4),
		/**
		 * For a stop job, enqueue a non-required dependency stop job on
		 * #to.
		 */
		kPropagatesStopTo = BITFLAG(5),
		/**
		 * For a restart job, enqueue a non-required dependency
		 * try-restart job on #to.
		 */
		kPropagatesRestartTo = BITFLAG(6),
		/**
		 * For a reload job, enqueue a non-required dependency
		 * try-reload job on #to.
		 */
		kPropagatesReloadTo = BITFLAG(7),

		/**
		 * @}
		 *
		 * \defgroup Post-Facto Enqueue Jobs After-The-Fact
		 * @{
		 */
		/**
		 * On unexpected start, enqueue a start job for #to.
		 */
		kStartOnStarted = BITFLAG(8),
		/**
		 * On unexpected start, enqueue a start job for #to if this
		 * won't reverse any immediately upcoming extant job.
		 */
		kTryStartOnStarted = BITFLAG(9),
		/**
		 * On unexpected start, enqueue a stop job for #to.
		 */
		kStopOnStarted = BITFLAG(10),
		/**
		 * On unexpected stop, enqueue a stop job for #to.
		 */
		kBoundBy = BITFLAG(11),

		/**
		 * @}
		 *
		 * \defgroup Events Enqueue Jobs in Response to State Changes
		 * @{
		 */
		/**
		 * On entering offline state from online state, enqueue a start
		 * job for #to.
		 */
		kOnSuccess = BITFLAG(12),
		/**
		 * On entering failed state, enqueue a start job for #to.
		 */
		kOnFailure = BITFLAG(13),

		/**
		 * @}
		 *
		 * \defgroup Ordering-and-Misc Ordering and Miscellaneous
		 * @{
		 */
		/**
		 * Attempt to run this job only after an existing job for #to
		 * has ran within a transaction.
		 */
		kAfter = BITFLAG(14),
		/**
		 * Attempt to run this job before an existing job for #to may
		 * run within a transaction.
		 */
		kBefore = BITFLAG(15), // FIXME: is this needed?

		/**
		 * @}
		 */
	};

    protected:
	Type type;			   /** Relationship type bitfield */
	std::shared_ptr<Schedulable> from; /** Owning object */
	std::shared_ptr<Schedulable> to;   /** Related object */

    public:
};

/* The base class of all entities which may be scheduled. */
class Schedulable {
	std::string name;
	std::list<std::unique_ptr<Edge>> edges; /* all edges from this node */

    public:
	typedef std::shared_ptr<Schedulable> SPtr;
};

/* A requirement from one job that another job completete successfully. */
class JobRequirement {
	Job *on;       /* on which job is the requirement? */
	bool required; /* whether this requiremen *must* be met */
};

/* A state-changing and/or state-querying task for a Schedulable Object. */
class Job {
	Schedulable::SPtr object; /* object on which to operate */
	std::unordered_set<JobRequirement> reqs;

    public:
	typedef std::unique_ptr<Job> UPtr;
};

/* A group of jobs in service of one job which defines the objective. */
class Transaction {
	std::unordered_set<Job::UPtr> jobs; /* all jobs in the tx */
	Job *objective; /* the job this tx aims to achieve */
};

/*
 * The scheduler itself.
 *
 * Transactions are organised into a queue. Unexpected object
 * state-change events will yield pseudotransactions; if any
 * transactions are pending, this pseudotransaction is merged into the
 * first pending transaction.
 */
class Scheduler {
	std::unordered_set<Schedulable::SPtr> objects;
	std::queue<std::unique_ptr<Transaction>> transactions;
};

#endif /* SCHEDULER_H_ */
