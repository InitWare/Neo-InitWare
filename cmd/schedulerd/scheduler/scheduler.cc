#include <cassert>
#include <iostream>

#include "../restarters/restarter.h"
#include "scheduler.h"

Edge::Edge(Type type, Schedulable::WPtr from, Schedulable::SPtr to)
    : type(type)
    , from(from)
    , to(to)
{
	to->edges_to.push_back(this);
}

Edge::~Edge()
{
	to->edges_to.remove(this);
}

Edge *
Schedulable::add_edge(Edge::Type type, SPtr to)
{
	edges.emplace_back(new Edge(type, shared_from_this(), to));
	return edges.back().get();
}

Schedulable::SPtr
Scheduler::add_object(Schedulable::SPtr obj)
{
	objects.emplace_back(obj);

	return obj;
}

bool
Scheduler::job_runnable(std::unique_ptr<Transaction::Job> &job)
{
	for (auto &dep : job->object->edges) {
		Transaction::Job *job2;

		if (!(dep->type & Edge::kAfter))
			continue;
		else if ((job2 = transactions.front()->job_for(dep->to)) !=
			NULL &&
		    job->after_order(job2) == 1) {
			std::cout << "Job " << *job << " must wait for "
				  << *job2 << " to complete\n";
			return false;
		}
	}

	return true;
}

int
Scheduler::enqueue_leaves(Transaction *tx)
{
	for (auto &it : tx->jobs) {
		auto &job = it.second;

		if (job_runnable(job)) {
			std::cout << *job << " may run\n";
			/*assert(job->object->restarter != nullptr);
			job->object->restarter->start_job(job->object,
			    job->type);*/
		}
	}

	return 0;
}

bool
Scheduler::enqueue_tx(Schedulable::SPtr object, Transaction::JobType op)
{
	transactions.emplace(std::make_unique<Transaction>(object, op));
	enqueue_leaves(transactions.front().get());
}

#pragma region Graph output
void
Edge::to_graph(std::ostream &out) const
{
	auto p = from.lock();
	out << p->m_name << " -> " << to->m_name;
	out << "[label=\"" << type_str() << "\"];\n";
}

void
Schedulable::to_graph(std::ostream &out) const
{
	out << m_name + ";\n";
	for (auto &edge : edges_to)
		edge->to_graph(out);
}

void
Scheduler::to_graph(std::ostream &out) const
{
	out << "digraph sched {\n";
	for (auto object : objects)
		object->to_graph(out);
	out << "}\n";
}

std::string
Edge::type_str() const
{
	/* clang-format off */
	static const std::map<int, const char *> types = {
		{kRequire, "Require"},
		{kWant, "Want"},
		{kRequisite, "Requisite"},
		{kConflict, "Conflict"},
		{kConflictedBy, "ConflictedBy"},
		{kPropagatesStopTo, "PropagatesStopTo"},
		{kPropagatesRestartTo, "PropagatesRestartTo"},
		{kPropagatesReloadTo, "PropagatesReloadTo"},
		{kStartOnStarted, "StartOnStarted"},
		{kTryStartOnStarted, "TryStartOnSTaretd"},
		{kStopOnStarted, "StopOnStarted"},
		{kBoundBy, "BoundBy"},
		{kOnSuccess, "OnSuccess"},
		{kOnFailure, "OnFailure"},
		{kAfter, "After"},
		{kBefore, "Before"}};
	/* clang-format on */
	std::string res;
	bool first = true;

	for (auto &entry : types) {
		if (type & entry.first) {
			if (!first)
				res += "\\n";
			res += entry.second;
			first = false;
		}
	}

	return std::move(res);
}

#pragma endregion

std::string &
Schedulable::state_str(State &state)
{
	/* clang-format off */
	static std::map<State, std::string> strs = {
		{ kUninitialised, "Uninitialised" },
		{ kOffline,	"Offline" },
		{ kStarting,	"Starting" },
		{ kOnline,	"Online" },
		{ kStopping,	"Stopping" },
		{ kMaintenance,	"Maintenance" },
		{ kMax,		"<invalid>" },
	};
	/* clang-format on */

	if (strs.find(state) == strs.end())
		state = kMax;

	return strs[state];
}
