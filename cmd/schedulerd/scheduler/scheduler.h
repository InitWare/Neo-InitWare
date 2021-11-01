#ifndef SCHEDULER_H_
#define SCHEDULER_H_

#include <algorithm>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../app/evloop.h"
#include "iwng_compat/misc_cxx.h"

using std::nullopt;
using std::optional;

class App;
class Job;
class Schedulable;
class Restarter;

#define BITFLAG(n) (1 << n)

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
	std::weak_ptr<Schedulable> from; /** Owning object */

    public:
	Type type;			 /** Relationship type bitfield */
	std::shared_ptr<Schedulable> to; /** Related object */

	Edge(Type type, std::weak_ptr<Schedulable> from,
	    std::shared_ptr<Schedulable> to);
	~Edge();

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

	struct Id {
		struct HashFn {
			std::size_t operator()(const Schedulable::Id &id) const
			{
				return std::hash<std::string>()(id.name);
			}
		};

		std::string name; /* full name of the object */

		Id(std::string name)
		    : name(name) {};

		bool operator==(const Id &other) const;
		bool operator==(const Schedulable::SPtr &obj) const;
	};

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
	Id id;
	std::list<std::unique_ptr<Edge>> edges; /**< edges from this node */
	std::list<Edge *> edges_to;		/**< edges to this node */
	Restarter *restarter;

    public:
	State state = kUninitialised;

	Schedulable(std::string name)
	    : id(name) {};

	Edge *add_edge(Edge::Type type, SPtr to);
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
		kStart,
		kVerify,
		kStop,
		kReload,
		kRestart,

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
		 * These are mainly of interest in jobs currently running.
		 */
		Id id = -1;
		Evloop::timerid_t timer; /**< job timeout timer id */
		State state = kAwaiting;

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
	std::multimap<Schedulable::SPtr, std::unique_ptr<Job>> jobs;
	Job *objective; /**< the job this tx aims to achieve */

	/**
	 * Add a new job including all of its dependencies.
	 */
	Job *submit_job(Schedulable::SPtr object, JobType op,
	    bool is_goal = false);

    private:
	bool is_cyclic(Schedulable::SPtr origin,
	    std::vector<Schedulable::SPtr> &path);
	/** Are all jobs on \p object required for the goal? */
	bool object_jobs_required(Schedulable::SPtr object);
	/** Try to find an object for which removing all jobs will end
	 * loop. */
	bool try_remove_cycle(std::vector<Schedulable::SPtr> &path);
	bool verify_acyclic();

	/** Delete all jobs on \p object. Jobs requiring these also
	 * deleted. */
	void del_jobs_for(Schedulable::SPtr object);

    public:
	Transaction(Schedulable::SPtr object, JobType op);

	/** First job (if any) for object. */
	Job *job_for(Schedulable::SPtr object);

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

	std::unordered_map<Schedulable::Id, Schedulable::SPtr,
	    Schedulable::Id::HashFn>
	    objects;
	std::queue<std::unique_ptr<Transaction>> transactions;
	std::unordered_map<Transaction::Job::Id, Transaction::Job *>
	    running_jobs;
	Transaction::Job::Id last_jobid = 0;

    public:
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
	int enqueue_leaves(Transaction *tx);

    public:
	Scheduler(App &app)
	    : app(app) {};

	Schedulable::SPtr add_object(Schedulable::SPtr obj);
	bool enqueue_tx(Schedulable::SPtr object, Transaction::JobType op);

	std::optional<Transaction::Job *> job_for_id(Transaction::Job::Id id);
	int job_complete(Transaction::Job::Id id, Transaction::Job::State res);

	int object_set_state(Schedulable::Id &id, Schedulable::State state);

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
