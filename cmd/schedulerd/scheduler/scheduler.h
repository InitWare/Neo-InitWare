#ifndef SCHEDULER_H_
#define SCHEDULER_H_

#include <algorithm>
#include <list>
#include <map>
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
};

/* The base class of all entities which may be scheduled. */
class Schedulable : public std::enable_shared_from_this<Schedulable> {
	friend class Scheduler;
	friend class Edge;

    protected:
	std::string m_name;
	std::list<std::unique_ptr<Edge>> edges; /**< edges from this node */
	std::list<Edge *> edges_to;		/**< edges to this node */

    public:
	typedef std::weak_ptr<Schedulable> WPtr;
	typedef std::shared_ptr<Schedulable> SPtr;

	Schedulable(std::string name)
	    : m_name(name) {};

	Edge *add_edge(Edge::Type type, SPtr to);
	template <typename T> T foreach_edge(T);
};

/* A group of jobs in service of one job which defines the objective. */
class Transaction {
	friend class EdgeVisitor;

    protected:
	/**
	 * A Job is set of state-changing and/or state-querying tasks for a
	 * Schedulable object. During a transaction's formation it may consist
	 * of multiple subjobs. These subjobs must all be mergeable or
	 * transaction generation will fail.
	 */
	struct Job {
		struct Subjob;

		enum Type {
			kStart,
			kVerify,
			kStop,
			kReload,
			kRestart,

			kTryRestart,	/**< restart if up, otherwise nop */
			kTryReload,	/**< reload if up, otherwise nop */
			kReloadOrStart, /**< reload if up, otherwise start */
		};

		/**
		 * A requirement from one subjob that [a subjob of] another job
		 * completete successfully.
		 */
		struct Requirement {
			Subjob *on;    /**< on which job is the requirement? */
			bool required; /**< whether this *must* be met */

			Requirement(Subjob *on, bool required)
			    : on(on)
			    , required(required) {};
			~Requirement();
		};

		struct Subjob {
			Job *job; /**< parent job */
			Type type;
			std::unordered_set<std::unique_ptr<Requirement>>
			    reqs; /**< requirements to other subjobs */
			std::unordered_set<Requirement *>
			    reqs_on; /**< requirements on this subjob */

			Subjob(Job *job, Type type)
			    : job(job)
			    , type(type) {};

			void add_req(Subjob *on, bool required);
		};

		std::unordered_set<std::unique_ptr<Subjob>> subjobs;
		Schedulable::SPtr object; /*< object on which to operate */

		Job(Schedulable::SPtr object, Type type, Subjob **sj = NULL)
		    : object(object)
		{
			subjobs.emplace_back(
			    std::make_unique<Subjob>(this, type));
			if (sj)
				*sj = subjobs.back().get();
		}
	};

	std::map<Schedulable::SPtr, std::unique_ptr<Job>> jobs;
	Job::Subjob *objective; /**< the job this tx aims to achieve */

	/**
	 * Add a new job including all of its dependencies.
	 */
	Job::Subjob *add_job_and_deps(Schedulable::SPtr object, Job::Type op,
	    Job::Subjob *requirer);

    public:
	Transaction(Schedulable::SPtr object, Job::Type op);
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

    public:
	Schedulable::SPtr add_object(Schedulable::SPtr obj);
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
