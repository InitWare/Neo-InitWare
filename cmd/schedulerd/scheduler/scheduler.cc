#include <cassert>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "../app/app.h"
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

bool
Schedulable::Id::operator==(const Id &other) const
{
	return name == other.name;
}

bool
Schedulable::Id::operator==(const Schedulable::SPtr &obj) const
{
	return name == obj->id.name;
}

Schedulable::SPtr
Scheduler::add_object(Schedulable::SPtr obj)
{
	assert(objects.find(obj->id) == objects.end());
	objects[obj->id] = obj;

	return obj;
}

int
Scheduler::job_run(Transaction::Job *job)
{
	std::cout << "Starting " << job->object->id.name << "\n";
	running_jobs[job->id] = job;
	app.restarters["target"]->start(job->id);
	app.add_timer(false, 700 /* JOB TIMEOUT MSEC */,
	    std::bind(&Scheduler::job_timeout_cb, this, std::placeholders::_1,
		std::placeholders::_2),
	    job->id);
	return true;
}

void
Scheduler::job_timeout_cb(Evloop::timerid_t id, uintptr_t udata)
{
	Transaction::Job::Id jid = udata;
	job_complete(jid, Transaction::Job::State::kTimeout);
}

bool
Scheduler::job_runnable(Transaction::Job *job)
{
	if (job->state != Transaction::Job::kAwaiting)
		return false;
	for (auto &dep : job->object->edges) {
		Transaction::Job *job2;

		if (!(dep->type & Edge::kAfter))
			continue;
		else if ((job2 = transactions.front()->job_for(dep->to)) !=
			NULL &&
		    job->after_order(job2) == 1) {
			std::cout << "Job " << *job << " must wait for "
				  << *job2 << " to complete\n";
			if (job2->state != Transaction::Job::State::kSuccess)
				return false;
		}
	}

	return true;
}

int
Scheduler::enqueue_leaves(Transaction *tx)
{
	for (auto &it : tx->jobs) {
		auto job = it.second.get();

		if (job->id == -1)
			job->id = last_jobid++;

		if (job_runnable(job)) {
			std::cout << *job << " is leaf, enqueueing\n";
			job_run(job);
		}
	}

	return 0;
}

bool
Scheduler::enqueue_tx(Schedulable::SPtr object, Transaction::JobType op)
{
	transactions.emplace(std::make_unique<Transaction>(object, op));
	enqueue_leaves(transactions.front().get());
	return true;
}

#define ANSI_CLEAR "\x1B[0m"
#define ANSI_HL_GREEN "\x1B[0;1;32m"
#define ANSI_HL_YELLOW "\x1B[0;1;31m"
#define ANSI_HL_RED "\x1B[0;1;33m"

static struct {
	std::string code, msg;
} job_msg[Transaction::Job::State::kMax] = {
	[Transaction::Job::State::kSuccess] = { ANSI_HL_GREEN, "  OK  " },
	[Transaction::Job::State::kFailure] = { ANSI_HL_RED, " Fail " },
	[Transaction::Job::State::kTimeout] = { ANSI_HL_RED, " Time " },
	[Transaction::Job::State::kCancelled] = { ANSI_HL_GREEN, "Cancel" },
};

int
Scheduler::job_complete(Transaction::Job::Id id, Transaction::Job::State res)
{
	auto &desc = job_msg[res];
	std::ostringstream msg_col;
	std::string code_col = "[" + desc.code + desc.msg + ANSI_CLEAR + "]";
	bool succ = false;
	auto &job = running_jobs[id];

	switch (res) {
	case Transaction::Job::State::kSuccess:
		msg_col << "Started ";
		succ = true;
		break;

	case Transaction::Job::State::kFailure:
		msg_col << "Failed to start ";
		break;

	case Transaction::Job::State::kTimeout:
		msg_col << "Timed out starting ";
		break;

	case Transaction::Job::State::kCancelled:
		msg_col << "Cancelled starting ";
		break;

	default:
		assert(!"unreached");
	}

	job->state = res;
	msg_col << job->object->id.name;

	std::cout << std::left << std::setw(67) << msg_col.str() << std::right
		  << std::setw(12) << code_col << "\n";

	for (auto &dep : job->object->edges_to) {
		Transaction::Job *job2;

		if (!(dep->type & Edge::kAfter))
			continue;
		else if ((job2 = transactions.front()->job_for(dep->to)) !=
			NULL &&
		    job_runnable(job2)) {
			std::cout << "Job " << *job2 << " may run now that "
				  << *job2 << " is complete\n";
			job_run(job2);
		}
	}

	return 0;
}

int
Scheduler::object_set_state(Schedulable::Id &id, Schedulable::State state)
{
	objects[id]->state = state;
}

#pragma region Graph output
void
Edge::to_graph(std::ostream &out) const
{
	auto p = from.lock();
	out << p->id.name << " -> " << to->id.name;
	out << "[label=\"" << type_str() << "\"];\n";
}

void
Schedulable::to_graph(std::ostream &out) const
{
	out << id.name + ";\n";
	for (auto &edge : edges_to)
		edge->to_graph(out);
}

void
Scheduler::to_graph(std::ostream &out) const
{
	out << "digraph sched {\n";
	for (auto object : objects)
		object.second->to_graph(out);
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
