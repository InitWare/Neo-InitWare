#ifndef SCHEDULER_H_
#define SCHEDULER_H_

#include <algorithm>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../app/evloop.h"
#include "iwng_compat/misc_cxx.h"
#include "object.h"

class App;
class Job;
class Scheduler;
class Restarter;

/** A task to be carried out. */
class Task : Printable<Task> {
    public:
	typedef int64_t Id;

	enum State {
		kAwaiting,  /**< Not yet started */
		kRunning,   /**< Currently running */
		kSuccess,   /**< Completed successfully */
		kFailure,   /**< Failed to complete*/
		kTimeout,   /**< Timed out awaiting completion  */
		kCancelled, /**< Job canceled before completion */
		kMax
	};

	enum Flags {
		/**
		 * Task may not be serialised and recreated during re-execution.
		 */
		kUnrecreatable = 1,
		/**
		 * Task is to be scheduled by the rules of object jobs.
		 */
		kJob = 2,
	};

	Id id = -1;		     /**< unique identifier */
	State state = kAwaiting;     /**< state of the task */
	Evloop::timerid_t timer = 0; /**< timeout timer id */
	Flags flags = (Flags)0;	     /**< bitmask of flags for this job */

	std::ostream &print(std::ostream &os) const;
};

/** A group of jobs in service of one job which defines the objective. */
class Transaction {
	friend class EdgeVisitor;
	friend class OrderVisitor;
	friend class Scheduler;

    public:
	enum JobType {
		kInvalid = -1,
		kStart,	  /**< start the object */
		kVerify,  /**< check the object is online */
		kStop,	  /**< stop the object */
		kReload,  /** reload the object */
		kRestart, /** stop then start the object */

		kTryStart,	 /**< as kStart, but only as requirement of
				    kTryRestart */
		kTryRestart,	 /**< restart if up, otherwise nop */
		kTryReload,	 /**< reload if up, otherwise nop */
		kReloadOrStart,	 /**< reload if up, otherwise start */
		kRestartOrStart, /**< restart if up, otherwise start */
		kMax,
	};

	/**
	 * A Job is a state-changing and/or state-querying task for a
	 * Schedulable object.
	 */
	struct Job : public Printable<Job>, public Task {
	    public:
		/**
		 * A requirement from one subjob that [a subjob of] another
		 * job completete successfully.
		 */
		struct Requirement {
			Job *from; /**< from which job is the requirement? */
			Job *to;       /**< on which job is the requirement? */
			bool required; /**< whether this *must* be met */
			bool goal_required; /**< whether goal requires it */

			/**
			 * Create a requirement.
			 */
			Requirement(Job *from, Job *on, bool required,
			    bool goal_required)
			    : from(from)
			    , to(on)
			    , required(required)
			    , goal_required(goal_required) {};
			/**
			 * Delete a requirement.
			 *
			 * The requirement is removed from \p to's reqs_on.
			 */
			~Requirement();
		};

		Schedulable::SPtr object; /*< object on which to operate */
		JobType type;		  /**< which operation to carry out */
		std::unordered_set<std::unique_ptr<Requirement>>
		    reqs; /**< requirements to other jobs */
		std::unordered_set<Requirement *>
		    reqs_on; /**< requirements on this job */
		bool goal_required = false; /**< is this required for goal? */

		Job(Schedulable::SPtr object, JobType type)
		    : object(object)
		    , type(type)
		{
		}
		~Job();

		/** Add a requirement on another subjob. */
		void add_req(Job *on, bool required, bool goal_required);

		/** Delete a requirement. As it calls Requirement's
		 * destructor, this removes the requirement from reqs
		 * and its to-node's reqs_on. */
		void del_req(Requirement *req);

		/**
		 * How should this job be ordered with respect to \p other given
		 * this job has a #kAfter dependency on that job?
		 *
		 * @retval -1 This should run before \p other
		 * @retval 1 \p other should run before this.
		 */
		int after_order(Job *other);

		void to_graph(std::ostream &out, bool edges) const;
		std::ostream &print(std::ostream &os) const;
	};

    protected:
	static const JobType merge_matrix[kMax][kMax];

	Scheduler &sched; /**< the scheduler this tx is associated with */
	std::map<Schedulable::SPtr, std::list<std::unique_ptr<Job>>>
	    jobs;	/**< maps objects to all jobs for that object */
	Job *objective; /**< the job this tx aims to achieve */

	/**
	 * Returns whichever job type results from merging types \p a and \p b,
	 * or #kInvalid if the merge is impossible.
	 */
	static JobType merged_job_type(JobType a, JobType b);

	/**
	 * Add a new job including all of its dependencies
	 * @param is_goal Whether this job is to be the goal job of the tx.
	 * @retval NULL Job could not be added.
	 * @retval Pointer to the created job.
	 */
	Job *job_submit(ObjectId id, JobType op, bool is_goal = false);
	/**
	 * As job_submit(ObjectId, JobType, bool) but directly referring to a
	 * schedulable entity.
	 * \see job_submit(ObjectId, JobType, bool)
	 */
	Job *job_submit(Schedulable::SPtr object, JobType op,
	    bool is_goal = false);

    private:
	typedef std::map<Schedulable::SPtr,
	    std::list<std::unique_ptr<Job>>>::iterator JobIterator;

	/** Fill \p dellist with all jobs to be deleted to
	 * remove a job (i.e. all requiring jobs). */
	void get_del_list(Job *job, std::vector<std::unique_ptr<Job>> &dellist);

	/**
	 * Tries to break a cycle (path of cycle indicated by \path) by finding
	 * an object whose jobs are not required for the goal, and deletes those
	 * jobs if so.
	 * @retval false if couldn't berak cycle
	 * @retval true if cycle broken by removing an object's jobs
	 */
	bool try_remove_cycle(std::vector<Schedulable::SPtr> &path);
	/**
	 * Verifies that the tranasction is acyclic. For each  cycles detected,
	 * tries to remove the cycle by calling try_remove_cycle().
	 * @retval true if transaction is (now) acyclic
	 * @retval false if transaction is cyclic
	 */
	bool verify_acyclic();

	int merge_job_into(Job *job, Job *into);
	int merge_jobs(std::list<std::unique_ptr<Job>> &to_merge);
	int merge_jobs();

	/**
	 * Determine whether an ordering cycle is created by the presence of
	 * a job for a given object.
	 * @retval false if no cycle fonud
	 * @retval true if cycle found, \p path contains the ordering path.
	 */
	bool object_creates_cycle(Schedulable::SPtr origin,
	    std::vector<Schedulable::SPtr> &path);
	/**
	 * Delete all jobs on \p object. Jobs requiring these also
	 * deleted.
	 */
	void object_del_jobs(Schedulable::SPtr object);
	/**
	 * Determines whether any of the jobs on \p object are required by, or
	 * are, the goal.
	 */
	bool object_requires_all_jobs(Schedulable::SPtr object);

    public:
	Transaction(Scheduler &sched, Schedulable::SPtr object, JobType op);

	/**
	 * Return the first job (if any) for a given object.
	 */
	Job *object_job_for(ObjectId object);
	/**
	 * As object_job_for(ObjectId).
	 * \see object_job_for(ObjectId)
	 */
	Job *object_job_for(Schedulable::SPtr object);

	static const char *type_str(JobType type);
	void to_graph(std::ostream &out) const;
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
    protected:
	App &app;

	std::unordered_set<Schedulable::SPtr> objects; /**< all objects */
	std::unordered_map<ObjectId, Schedulable::SPtr, ObjectId::HashFn>
	    m_aliases; /**< maps all names to an associated object */
	std::queue<ObjectId> m_loadqueue; /**< object IDs to be loaded */
	std::queue<std::unique_ptr<Transaction>>
	    transactions; /**< the transaction queue */
	std::unordered_map<Transaction::Job::Id, Transaction::Job *>
	    running_jobs;		     /**< jobs currently running */
	Transaction::Job::Id last_jobid = 0; /**< job id counter */

    private:
	/** Invoke restarter & places the job in the #running_jobs map. */
	int job_run(Transaction::Job *job);
	/**
	 * Is this job ready to run? Namely, are there any jobs pending in the
	 * currently-running transaction which must come before it?
	 */
	bool job_runnable(Transaction::Job *job);
	/** Called when a job has timed out. */
	void job_timeout_cb(Evloop::timerid_t id, uintptr_t udata);

	/**
	 * Remap all edges from to and to an object, which are not owned by that
	 * object, to another object. Moves the edges as necessary.
	 */
	void object_remap_unowned_edges(Schedulable::SPtr obj,
	    Schedulable::SPtr newobj);

	/** Enqueue the set of leaf jobs ready to start immediately. */
	int tx_enqueue_leaves(Transaction *tx);

	/** Log that a job has completed. */
	void log_job_complete(Transaction::Job * job);

    public:
	/** Create a new scheduler as part of a given app. */
	Scheduler(App &app)
	    : app(app) {};

	void dispatch_load_queue();

	/**
	 * Add an edge from one object to another. If the to-node does not
	 * exist, a placeholder is created.
	 */
	Edge *edge_add(Edge::Type type, ObjectId owner, ObjectId from,
	    ObjectId to);

	/**
	 * Get the job matching the given ID, if there is one.
	 */
	Transaction::Job *job_get(Transaction::Job::Id id);
	/**
	 * Notify the scheduler of the completion of a job.
	 */
	int job_complete(Transaction::Job::Id id, Transaction::Job::State res);
	/** @} */

	/**
	 * Add an object created outwith the scheduler.
	 */
	Schedulable::SPtr object_add(Schedulable::SPtr obj);
	/**
	 * As object_add(Schedulable::SPtr) but with an explicit main alias.
	 */
	Schedulable::SPtr object_add(ObjectId id, Schedulable::SPtr obj);
	/**
	 * Retrieve the object matching the identifier, if none is found,
	 * one is created and added to the load queue.
	 * TODO: an "object_find" that simply finds an existing object or
	 * NULL?
	 */
	Schedulable *object_get(ObjectId &id);
	/**
	 * Load an object into the scheduler as defined by its set of
	 * aliases, a map of distal node identifiers to edge masks to create
	 * edges to, and map of proximal node identifiers to edge masks to
	 * create edges from.
	 */
	void object_load(std::vector<std::string> aliases,
	    std::map<std::string, Edge::Type> edges_from,
	    std::map<std::string, Edge::Type> edges_to);
	/**
	 * Notify the scheduler that an object has changed state. This is a
	 * orthogonal to the jobs system; state changes notified by this
	 * means give rise to automatic transactions generated by the
	 * event-driven impurity.
	 */
	int object_set_state(ObjectId &id, Schedulable::State state);

	/**
	 * Generate and enqueue a transaction.
	 * @retval 0 Transaction successfully enqueued
	 * @retval -errno Transaction creation or enqueuing failed.
	 */
	bool tx_enqueue(Schedulable::SPtr object, Transaction::JobType op);

	void to_graph(std::ostream &out) const;
};

#endif /* SCHEDULER_H_ */
