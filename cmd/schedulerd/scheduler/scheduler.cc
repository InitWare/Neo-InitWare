#include <cassert>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "../app/app.h"
#include "../restarters/restarter.h"
#include "iwng_compat/misc.h"
#include "scheduler.h"

Edge::Edge(ObjectId owner, Type type, ObjectId from, ObjectId to)
    : owner(owner)
    , type(type)
    , from(from)
    , to(to)
{
}

Edge::~Edge()
{
}

const ObjectId &
Schedulable::id() const
{
	return ids.front();
}

bool
ObjectId::operator==(const ObjectId &other) const
{
	return name == other.name;
}

bool
ObjectId::operator==(const Schedulable::SPtr &obj) const
{
	return name == obj->id().name;
}

Schedulable::SPtr
Scheduler::object_add(Schedulable::SPtr obj)
{
	return object_add(obj->id(), obj);
}

Schedulable::SPtr
Scheduler::object_add(ObjectId id, Schedulable::SPtr obj)
{
	assert(objects.find(obj) == objects.end());
	assert(m_aliases.find(id) == m_aliases.end() ||
	    m_aliases.find(id)->second == obj);
	objects.emplace(obj);
	m_aliases[id] = obj;

	return obj;
}

Edge *
Scheduler::edge_add(Edge::Type type, ObjectId owner, ObjectId from, ObjectId to)
{
	auto oowner = object_get(from), ofrom = object_get(from),
	     oto = object_get(to);

	ofrom->edges.emplace_back(
	    std::make_unique<Edge>(owner, type, from, to));
	oto->edges_to.emplace_back(ofrom->edges.back().get());

	return ofrom->edges.back().get();
}

int
Scheduler::job_run(Transaction::Job *job)
{
	std::cout << "Starting " << job->object->id().name << "\n";
	running_jobs[job->id] = job;
	job->timer = app.add_timer(false, 700 /* JOB TIMEOUT MSEC */,
	    std::bind(&Scheduler::job_timeout_cb, this, std::placeholders::_1,
		std::placeholders::_2),
	    job->id);
	app.restarters["target"]->start(job->id);
	return true;
}

void
Scheduler::job_timeout_cb(Evloop::timerid_t id, uintptr_t udata)
{
	Transaction::Job::Id jid = udata;

	running_jobs[jid]->timer = 0;
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
		else if ((job2 = transactions.front()->object_job_for(
			      dep->to)) != NULL &&
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
Scheduler::tx_enqueue_leaves(Transaction *tx)
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
Scheduler::tx_enqueue(Schedulable::SPtr object, Transaction::JobType op)
{
	transactions.emplace(std::make_unique<Transaction>(*this, object, op));
	tx_enqueue_leaves(transactions.front().get());
	return true;
}

int
Scheduler::job_complete(Transaction::Job::Id id, Transaction::Job::State res)
{
	auto job = running_jobs[id];

	if (job->timer != 0)
		app.del_timer(job->timer);
	running_jobs.erase(id);
	log_job_complete(job->object->id(), res);
	job->state = res;

	if (res == Transaction::Job::State::kSuccess &&
	    job->type == Transaction::JobType::kRestart) {
		/* restart jobs are converted to start jobs on success */
		job->type = Transaction::JobType::kStart;
		job->state = Transaction::Job::kAwaiting;
		if (job_runnable(job))
			job_run(job);
		goto start_others;
	}

	/*
	 * TODO: We need to make a design decision. Should we go through all the
	 * jobs with requirement edge to the completed job, and fail them if
	 * they have req=1, or should we simply rely on PropagateStopTo
	 * dependencies instead?
	 */

start_others:
	/*
	 * For each object which has an ordering edge to the object whose job
	 * has now completed, we check if a job exists for that object
	 * within the transaction; if there is, we check if the job is
	 * runnable, and run it if so.
	 */
	for (auto &dep : job->object->edges_to) {
		Transaction::Job *job2;

		if (!(dep->type & Edge::kAfter))
			continue;
		else if ((job2 = transactions.front()->object_job_for(
			      dep->from)) != NULL &&
		    job_runnable(job2)) {
			std::cout << "Job " << *job2 << " may run now that "
				  << *job2 << " is complete\n";
			job_run(job2);
		}
	}

	return 0;
}

Schedulable *
Scheduler::object_get(ObjectId &id)
{
	auto it = m_aliases.find(id);
	if (it != m_aliases.end())
		return it->second.get();
	else
		return nullptr;
}

void
Scheduler::object_load(std::vector<std::string> aliases,
    std::map<std::string, Edge::Type> edges_from,
    std::map<std::string, Edge::Type> edges_to)
{
	Schedulable::SPtr obj;

	for (auto &alias : aliases)
		m_aliases.erase(alias);

	if (obj == NULL)
		obj = std::make_shared<Schedulable>();

	for (auto &alias : aliases)
		obj->ids.push_back(alias);

	for (auto &edge : edges_from) {
		std::cout << "Edge " << Edge::type_str(edge.second) << " to "
			  << edge.first << "\n";
		// obj->add_edge(edge.second, edge.first);
	}

	for (auto &edge : edges_to) {
		std::cout << "Edge " << Edge::type_str(edge.second) << " from "
			  << edge.first << "\n";
		// obj->add_edge(edge.second, edge.first);
	}
}

int
Scheduler::object_set_state(ObjectId &id, Schedulable::State state)
{
	m_aliases[id]->state = state;
	return 0;
}

#pragma region Logging

static struct {
	std::string code, msg;
} job_complete_msg[Transaction::Job::State::kMax] = {
	[Transaction::Job::State::kAwaiting] = {},
	[Transaction::Job::State::kSuccess] = { ANSI_HL_GREEN, "  OK  " },
	[Transaction::Job::State::kFailure] = { ANSI_HL_RED, " Fail " },
	[Transaction::Job::State::kTimeout] = { ANSI_HL_RED, " Time " },
	[Transaction::Job::State::kCancelled] = { ANSI_HL_GREEN, "Cancel" },
};

void
Scheduler::log_job_complete(ObjectId id, Transaction::Job::State res)
{
	auto &desc = job_complete_msg[res];
	std::ostringstream msg_col;
	std::string code_col = "[" + desc.code + desc.msg + ANSI_CLEAR + "]";
	bool succ = false;

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

	msg_col << id.name;

	std::cout << std::left << std::setw(67) << msg_col.str() << std::right
		  << std::setw(12) << code_col << "\n";
}

#pragma endregion

#pragma region Graph output
void
Edge::to_graph(std::ostream &out) const
{
	out << from.name << " -> " << to.name;
	out << "[label=\"" << type_str() << "\"];\n";
}

void
Schedulable::to_graph(std::ostream &out) const
{
	out << id().name + ";\n";
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
	return type_str(type);
}

std::string
Edge::type_str(Type type)
{
	/* clang-format off */
	static const std::map<int, const char *> types = {
		{kAddStart, "Require"},
		{kAddStartNonreq, "Want"},
		{kAddVerify, "Requisite"},
		{kAddStop, "Conflict"},
		{kAddStopNonreq, "ConflictedBy"},
		{kPropagatesStopTo, "PropagatesStopTo"},
		{kPropagatesRestartTo, "PropagatesRestartTo"},
		{kPropagatesReloadTo, "PropagatesReloadTo"},
		{kStartOnStarted, "StartOnStarted"},
		{kTryStartOnStarted, "TryStartOnStarted"},
		{kStopOnStarted, "StopOnStarted"},
		{kStopOnStopped, "BoundBy"},
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
