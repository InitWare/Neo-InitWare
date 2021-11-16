#ifndef OBJECT_H_
#define OBJECT_H_

#include <algorithm>
#include <list>
#include <memory>
#include <string>
#include <unordered_set>

#include "iwng_compat/misc.h"

class Schedulable;

/** A unique identifier for an object. An object may have many of these. */
struct ObjectId {
	struct HashFn {
		std::size_t operator()(const ObjectId &id) const
		{
			return std::hash<std::string>()(id.name);
		}
	};

	std::string name; /**< full name of the object */

	ObjectId(std::string name)
	    : name(name) {};
	ObjectId(const char *name)
	    : name(name) {};

	bool operator==(const ObjectId &other) const;
	bool operator==(const std::shared_ptr<Schedulable> &obj) const;
};

/** An edge between two entities in the Schedulable Objects Graph. */
class Edge {
	friend class Schedulable;
	friend class Scheduler;

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
	Type type; /**< Relationship type bitfield */

	ObjectId owner; /**< Object whose configuration introduced this edge */
	ObjectId from;	/**< Proximal object */
	ObjectId to;	/**< Distal object */

	Edge(ObjectId owner, Type type, ObjectId from, ObjectId to)
	    : owner(owner)
	    , type(type)
	    , from(from)
	    , to(to) {};

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
	friend class ObjectId;

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
	ObjectId main_alias; /**< main identifier */
	std::unordered_set<ObjectId, ObjectId::HashFn>
	    aliases; /**< all identifiers of the node */
	std::list<std::unique_ptr<Edge>> edges; /**< edges from this node */
	std::list<Edge *> edges_to;		/**< edges to this node */

    public:
	State state = kUninitialised;

	Schedulable(std::string name)
	    : main_alias(name)
	{
		aliases.insert(name);
	};

	/** Get the principal name of this node. */
	const ObjectId &id() const;

	template <typename T> T foreach_edge(T);

	void to_graph(std::ostream &out) const;
	std::string &state_str(State &state);
};

/*
 * templates/inlines
 */

/** Invoke a functor for each edge from an object. */
template <typename T>
T
Schedulable::foreach_edge(T functor)
{
	return std::for_each(edges.begin(), edges.end(), functor);
}

#endif /* OBJECT_H_ */
