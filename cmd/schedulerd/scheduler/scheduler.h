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

class App;
class Job;
class Schedulable;
class Scheduler;
class Restarter;

#define BITFLAG(n) (1 << n)

struct ObjectId {
	struct HashFn {
		std::size_t operator()(const ObjectId &id) const
		{
			return std::hash<std::string>()(id.name);
		}
	};

	std::string name; /* full name of the object */

	ObjectId(std::string name)
	    : name(name) {};
	ObjectId(const char *name)
	    : name(name) {};

	bool operator==(const ObjectId &other) const;
	bool operator==(const std::shared_ptr<Schedulable> &obj) const;
};

/** An edge between two entities in the Schedulable Objects Graph. */
class Edge {
    public:
	class Visitor {
		virtual void operator()(std::unique_ptr<Edge> &edge) = 0;
	};

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
		kAddStart = BITFLAG(0),
		/**
		 * For a start job, enqueue a non-required dependency start job
		 * on #to, but ignore result.
		 */
		kAddStartNonreq = BITFLAG(1),
		/**
		 * For a start job, enqueue a required dependency verify (check
		 * if online) job on #to, and fail if that fails.
		 */
		kAddVerify = BITFLAG(2),
		/**
		 * For a start job, enqueue a required dependency stop job on
		 * #to.
		 */
		kAddStop = BITFLAG(3),
		/**
		 * For a start job, enqueue a non-required dependency stop job
		 * on #to.
		 */
		kAddStopNonreq = BITFLAG(4),
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
		kStopOnStopped = BITFLAG(11),

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

    public:
	Type type;			 /** Relationship type bitfield */

	ObjectId owner;
	ObjectId from; /** Proximal object */
	ObjectId to;   /** Distal object */

	Edge(ObjectId owner, Type type, ObjectId from, ObjectId to);
	~Edge();

	static std::string type_str(Type);
	std::string type_str() const;

	void to_graph(std::ostream &out) const;
};

/* The base class of all entities which may be scheduled. */
class Schedulable : public std::enable_shared_from_this<Schedulable> {
	friend class Scheduler;
	friend class Edge;
	friend class Transaction;
	friend class EdgeVisitor;

    public:
	typedef std::weak_ptr<Schedulable> WPtr;
	typedef std::shared_ptr<Schedulable> SPtr;

	enum State {
		kUninitialised, /**< not [yet] loaded */
		kOffline,	/**< not up */
		kStarting,	/**< going up */
		kOnline,	/**< up */
		kStopping,	/**< going down */
		kMaintenance,	/**< error occurred */
		kMax,
	};

    protected:
	std::list<ObjectId> ids; /**< all identifiers of the node */
	std::list<std::unique_ptr<Edge>> edges; /**< edges from this node */
	std::list<Edge *> edges_to;		/**< edges to this node */
	Restarter *restarter;

    public:
	State state = kUninitialised;

	Schedulable() {};
	Schedulable(std::string name) { ids.push_back(name); };

	/** Get the principal name of this node. */
	const ObjectId &id() const;

	template <typename T> T foreach_edge(T);

	void to_graph(std::ostream &out) const;
	std::string &state_str(State &state);
};

/* A group of jobs in service of one job which defines the objective. */
class Transaction {
	friend class EdgeVisitor;
	friend class OrderVisitor;
	friend class Scheduler;

    public:
	enum JobType {
		/**
		 * \defgroup regular Regular job types
		 * @{
		 */
		kStart,
		kVerify,
		kStop,
		kReload,
		kRestart,
		/** @} */

		kTryStart,	/**< as kStart, but only as requirement of
				   kTryRestart */
		kTryRestart,	/**< restart if up, otherwise nop */
		kTryReload,	/**< reload if up, otherwise nop */
		kReloadOrStart, /**< reload if up, otherwise start */
		kMax,
	};

	/**
	 * A Job is a state-changing and/or state-querying task for a
	 * Schedulable object.
	 */
	struct Job : public Printable<Job> {
	    public:
		typedef int64_t Id;

		enum State {
			kAwaiting,
			kSuccess,
			kFailure,
			kTimeout,
			kCancelled,
			kMax
		};

		/**
		 * A requirement from one subjob that [a subjob of] another
		 * job completete successfully.
		 */
		struct Requirement {
			Job *from;
			Job *to;       /**< on which job is the requirement? */
			bool required; /**< whether this *must* be met */
			bool goal_required; /**< whether *must* be met for goal
					     */

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
		JobType type;
		std::unordered_set<std::unique_ptr<Requirement>>
		    reqs; /**< requirements to other jobs */
		std::unordered_set<Requirement *>
		    reqs_on; /**< requirements on this job */
		bool goal_required = false;

		/**
		 * \defgroup runstate State for running jobs
		 * @{
		 */
		Id id = -1;		     /**< unique ID */
		Evloop::timerid_t timer = 0; /**< timeout timer id */
		State state = kAwaiting;     /**< state */
		/** @} */

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

		/** Fill \p dellist with all jobs to be deleted to
		 * remove this job (i.e. all requiring jobs). */
		void get_del_list(std::vector<Job *> &dellist);

		/**
		 * How should this job be ordered with respect to \p other given
		 * this job has a kAfter dependency on that job?
		 *
		 * @retval -1 This should run before \p other
		 * @retval 1 \p other should run before this.
		 */
		int after_order(Job *other);

		void to_graph(std::ostream &out, bool edges) const;
		std::ostream &print(std::ostream &os) const;
	};

    protected:
	Scheduler &sched;
	std::multimap<Schedulable::SPtr, std::unique_ptr<Job>> jobs;
	Job *objective; /**< the job this tx aims to achieve */

	/**
	 * @brief Add a new job including all of its dependencies
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

	std::unordered_set<Schedulable::SPtr> objects;
	std::unordered_map<ObjectId, Schedulable::SPtr, ObjectId::HashFn>
	    m_aliases;
	std::queue<std::unique_ptr<Transaction>> transactions;
	std::unordered_map<Transaction::Job::Id, Transaction::Job *>
	    running_jobs;
	Transaction::Job::Id last_jobid = 0;

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

	/** Enqueue the set of leaf jobs ready to start immediately. */
	int tx_enqueue_leaves(Transaction *tx);

	void log_job_complete(ObjectId id, Transaction::Job::State res);

    public:
	Scheduler(App &app)
	    : app(app) {};

	/**
	 * Add an edge from one object to another. If the to-node does not
	 * exist, a placeholder is created.
	 */
	Edge *edge_add(Edge::Type type, ObjectId owner, ObjectId from,
	    ObjectId to);

	/**
	 * @defgroup jobs Job management
	 * @{
	 */
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
	 * @defgroup objects Object management
	 * @{
	 */
	/**
	 * Add an object created outwith the scheduler.
	 */
	Schedulable::SPtr object_add(Schedulable::SPtr obj);
	/**
	 * As object_add(Schedulable::SPtr) but with an explicit main alias.
	 */
	Schedulable::SPtr object_add(ObjectId id, Schedulable::SPtr obj);
	/**
	 * Retrieve the object matching the identifier, if there is one.
	 * @retval NULL no such object
	 * @retval Schedulable matching object
	 */
	Schedulable *object_get(ObjectId &id);
	/**
	 * Load an object into the scheduler as defined by its set of aliases, a
	 * map of distal node identifiers to edge masks to create edges to, and
	 * map of proximal node identifiers to edge masks to create edges from.
	 */
	void object_load(std::vector<std::string> aliases,
	    std::map<std::string, Edge::Type> edges_from,
	    std::map<std::string, Edge::Type> edges_to);
	/**
	 * Notify the scheduler that an object has changed state. This is a
	 * orthogonal to the jobs system; state changes notified by this means
	 * give rise to automatic transactions generated by the event-driven
	 * impurity.
	 */
	int object_set_state(ObjectId &id, Schedulable::State state);
	/** @} */

	/**
	 * Generate and enqueue a transaction.
	 * @retval 0 Transaction successfully enqueued
	 * @retval -errno Transaction creation or enqueuing failed.
	 */
	bool tx_enqueue(Schedulable::SPtr object, Transaction::JobType op);

	void to_graph(std::ostream &out) const;
};

/*
 * templates/inlines
 */

template <typename T>
T
Schedulable::foreach_edge(T functor)
{
	return std::for_each(edges.begin(), edges.end(), functor);
}

#endif /* SCHEDULER_H_ */
