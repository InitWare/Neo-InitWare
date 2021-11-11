#include <algorithm>
#include <cassert>
#include <iostream>

#include "scheduler.h"

Transaction::Transaction(Scheduler &sched, Schedulable::SPtr object, JobType op)
    : sched(sched)
{
	objective = job_submit(object, op, true);
	// to_graph(std::cout);
	if (!verify_acyclic())
		throw("Transaction is unresolveably cyclical");
	if (merge_jobs() < 0)
		throw("Transaction contains unmergeable jobs");
	// to_graph(std::cout);
}

Transaction::Job::~Job()
{
	/** remove all requirements on this subjob from others' reqs */
	for (auto req_on = reqs_on.begin(); req_on != reqs_on.end();) {
		auto old = *req_on;
		req_on = reqs_on.erase(req_on);
		old->from->del_req(old);
	}
}

Transaction::Job::Requirement::~Requirement()
{
	to->reqs_on.erase(this);
}

void
Transaction::Job::add_req(Job *on, bool required, bool goal_required)
{
	auto req = std::make_unique<Requirement>(this, on, required,
	    goal_required);
	auto reqp = req.get();
	reqs.emplace(std::move(req));
	on->reqs_on.emplace(reqp);
}

void
Transaction::Job::del_req(Requirement *reqp)
{
	for (auto &req : reqs) {
		if (req.get() == reqp) {
			req->to->reqs_on.erase(req.get());
			reqs.erase(req);
			return;
		}
	}
	throw("Requirement not found");
}

int
Transaction::Job::after_order(Job *other)
{
	if (among(other->type, { kStop, kRestart }))
		return -1; /* stop/restart jobs have reverse ordering */
	else
		return 1; /* ordinary ordering for other job types */
}

void
Transaction::get_del_list(Job *job, std::vector<std::unique_ptr<Job>> &dellist)
{
	/** TODO: Should also remove any jobs wanted solely by \p job */

	for (auto &req_on : job->reqs_on) {
		if (req_on->required) {
			get_del_list(req_on->from, dellist);
		}
	}

	for (auto it = jobs[job->object].begin(); it != jobs[job->object].end();
	     it++)
		if (it->get() == job) {
			dellist.emplace_back(std::move(*it));
			jobs[job->object].erase(it);
			return;
		}
}

/** Delete all jobs on \p object. */
void
Transaction::object_del_jobs(Schedulable::SPtr object)
{
#if 0
	std::vector<Job *> dellist;

	for (auto &job : jobs[object])
		job->get_del_list(dellist);

	for (auto job : dellist)
		jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
		    UniquePtrEq<Job>(job)));
#endif

	std::vector<std::unique_ptr<Job>> dellist;

	for (auto it = jobs[object].begin(); it != jobs[object].end();) {
		get_del_list(it->get(), dellist);
		// dellist.emplace_back(std::move(*it));
		// it = jobs[object].erase(it);
	}
}

Transaction::Job *
Transaction::object_job_for(Schedulable::SPtr object)
{
	auto it = jobs.find(object);
	return it == jobs.end() ? nullptr :
	    it->second.empty()	? nullptr :
					it->second.front().get();
}

Transaction::Job *
Transaction::object_job_for(ObjectId id)
{
	for (auto &pair : jobs) {
		if (id == pair.first)
			return pair.second.empty() ? nullptr :
							   pair.second.front().get();
	}
	return nullptr;
}

#pragma region Order loop detection &recovery

class OrderVisitor : public Edge::Visitor {
    public:
	OrderVisitor(Transaction &tx, std::vector<Schedulable::SPtr> &path,
	    bool &cyclic)
	    : tx(tx)
	    , path(path)
	    , cyclic(cyclic) {};

	void operator()(std::unique_ptr<Edge> &edge);

    private:
	Transaction &tx;
	std::vector<Schedulable::SPtr> &path;
	bool &cyclic;
};

void
OrderVisitor::operator()(std::unique_ptr<Edge> &edge)
{
	if (edge->type & Edge::kAfter && !cyclic) {
		if (tx.object_job_for(edge->to) != nullptr) {
			/* job(s) exist for this After edge - check for loop */
			if (tx.object_creates_cycle(
				tx.sched.object_get(edge->to)
				    ->shared_from_this(), // fixme ugly
				path))
				cyclic = true;
		}
	}
}

/**
 * Visits the object's ordering edges, where a #Job::kAfter edge (or a
 * from-edge of an #Edge::kBefore) exists and the other node has a job
 * present in the transaction, this function calls itself with that node
 * as \p origin and the same \p path.
 *
 * At the start of each invocation \p path is searched to see if that object is
 * already present; if so, a cycle is indicated and true is returned. Otherwise
 * the object is added to \p path and edges visited as described.
 */
bool
Transaction::object_creates_cycle(Schedulable::SPtr job,
    std::vector<Schedulable::SPtr> &path)
{
	bool cyclic = false;

	if (std::find(path.begin(), path.end(), job) != path.end())
		return true;

	path.push_back(job);
	job->foreach_edge(OrderVisitor(*this, path, cyclic));

	if (!cyclic)
		path.pop_back();

	return cyclic;
}

bool
Transaction::object_requires_all_jobs(Schedulable::SPtr object)
{
	for (auto &job : jobs[object]) {

		if (job.get() == objective) {
			std::cout << "not deleting " << *job
				  << "; is objective\n";
			return true;
		}

		for (auto req_on : job->reqs_on) {
			if (req_on->goal_required) {
				std::cout
				    << "not deleting " << *job
				    << "; is required by goal-essential job "
				    << *req_on->from << "\n";

				return true;
			}
		}
	}

	return false;
}

bool
Transaction::try_remove_cycle(std::vector<Schedulable::SPtr> &path)
{
	for (auto &job : reverse(path)) {
		bool essential = false;

		if (!object_requires_all_jobs(job)) {
			std::cout << "Cycle resolved: deleting jobs on "
				  << job->id().name
				  << " as non-essential to goal.\n";
			object_del_jobs(job);
			return true;
		}
	}

	std::cout << "Cycle unresolveable.";

	return false;
}

bool
Transaction::verify_acyclic()
{
restart:
	for (auto &job : jobs) {
		std::vector<Schedulable::SPtr> path;
		if (object_creates_cycle(job.second.front()->object, path)) {
			printf("CYCLE DETECTED:\n");
			for (auto &obj : path)
				printf("%s -> ", obj->id().name.c_str());
			printf("%s\n", path.front()->id().name.c_str());
			if (!try_remove_cycle(path))
				return false;
		}
	}
	return true;
}

/* clang-format off */
const Transaction::JobType Transaction::merge_matrix[kMax][kMax] = {
	/* 		START		VERIFY		STOP		RELOAD		RESTART		TRYSTART	TRYRESTART	TRYRELOAD	RELOADORSTART */
	[kStart] = {},
	[kVerify] = {	kStart,	 },
	[kStop] = {	kInvalid,	kInvalid },
	[kReload] = {	kStart, 	kStart,		kInvalid, },
	[kRestart] = {	kRestart,	kRestart,	kInvalid, 	kRestart },

	[kTryStart] = {	kStart,		kStart,		kInvalid,	kReloadOrStart,	kRestart },
	[kTryRestart] = { kRestartOrStart,kRestartOrStart,kInvalid,	kTryRestart,	kRestart,	kRestartOrStart, },
	[kTryReload] = { kReloadOrStart,kReloadOrStart,	kInvalid,	kReload,	kRestart,	kReloadOrStart, kRestartOrStart },
	[kReloadOrStart] = { kReloadOrStart,kReloadOrStart,kInvalid,	kReloadOrStart, kRestartOrStart,kReloadOrStart,	kReloadOrStart, kReloadOrStart },
	[kRestartOrStart] = { kRestartOrStart,kRestartOrStart,kInvalid,	kRestartOrStart,kRestartOrStart,kRestartOrStart,kRestartOrStart,kRestartOrStart,kRestartOrStart},
};
/* clang-format on */

Transaction::JobType
Transaction::merged_job_type(JobType a, JobType b)
{
	if (a == b)
		return a;
	else if (a > b)
		return merge_matrix[a][b];
	else
		return merge_matrix[b][a];
}

int
Transaction::merge_job_into(Job *job, Job *into)
{
	/* TODO: could check if existing reqs present? does it matter? */
	for (auto it = job->reqs.begin(); it != job->reqs.end(); it++) {
		(*it)->from = into;
	}

	into->reqs.merge(job->reqs);

	for (auto &req : job->reqs_on) {
		req->to = into;
	}

	into->reqs_on.merge(job->reqs_on);

	if (job->goal_required)
		into->goal_required = true;

	return 0;
}

int
Transaction::merge_jobs(std::list<std::unique_ptr<Job>> &to_merge)
{
	do {
		auto it1 = to_merge.begin(), it2 = ++to_merge.begin();
		auto &j1 = *it1, &j2 = *it2;
		JobType merged;

		merged = merged_job_type(j1->type, j2->type);

		if (merged == kInvalid) {
			auto jtodel = to_merge.begin();
			std::vector<std::unique_ptr<Job>> dellist;
			bool del_2 = false;

			std::cout << "Jobs " << *j1 << " and " << *j2
				  << " are unmergeable\n";

			if (j1->goal_required && j2->goal_required) {
				std::cout
				    << "Both are goal-required; merge failed.\n";
				return -1;
			} else if (!j1->goal_required && !j2->goal_required) {
				if (j2->type == kStop)
					del_2 = true;
			} else if (!j2->goal_required)
				del_2 = true;

			if (del_2)
				jtodel = it2;
			else
				jtodel = it1;

			std::cout << "Selected " << **jtodel << " to delete.\n";

			get_del_list(jtodel->get(), dellist);

			for (auto &job : dellist)
				std::cout << " -> Deleting " << *job << "\n";
		} else {
			std::cout << "Jobs " << *j1 << " and " << *j2
				  << " merged to form " << type_str(merged)
				  << "\n";
			merge_job_into(j1.get(), j2.get());
			to_merge.pop_front();
		}

	} while (to_merge.size() > 1);

	return 0;
}

int
Transaction::merge_jobs()
{
	bool first = true;
	JobIterator start_of_obj;

	std::cout << "Merging jobs begins.\n";

	for (auto &group : jobs) {
		if (group.second.size() > 1)
			if (merge_jobs(group.second) < 0)
				return -1;
	}

	std::cout << "Merging jobs ends.\n";

	return 0;
}

#pragma endregion

#pragma region TX Generation

class EdgeVisitor : public Edge::Visitor {
    public:
	EdgeVisitor(Edge::Type type, Transaction::JobType op, Transaction &tx,
	    Transaction::Job *requirer, bool is_required)
	    : type(type)
	    , op(op)
	    , tx(tx)
	    , requirer(requirer)
	    , is_required(is_required)
	{
	}

	void operator()(std::unique_ptr<Edge> &edge);

    private:
	Edge::Type type;
	Transaction::JobType op;
	Transaction &tx;
	Transaction::Job *requirer;
	bool is_required;
};

void
EdgeVisitor::operator()(std::unique_ptr<Edge> &edge)
{
	if (edge->type & type) {
		bool goal_required = (requirer->goal_required) && is_required;
		Transaction::Job *sj = tx.job_submit(edge->to, op,
		    goal_required);

		requirer->add_req(sj, is_required, goal_required);
	}
}

Transaction::Job *
Transaction::job_submit(ObjectId id, JobType op, bool goal_required)
{
	auto object = sched.object_get(id);

	if (!object) {
		std::cout << "No object for ID " + id.name + "\n";
		return NULL;
	} else
		return job_submit(object->shared_from_this(), op,
		    goal_required);
}

Transaction::Job *
Transaction::job_submit(Schedulable::SPtr object, JobType op,
    bool goal_required)
{
	Job *sj = NULL;	     /* newly created or existing subjob */
	bool exists = false; /* whether the subjob already exists */

	std::cout << "Submitting job on object " + object->id().name + "\n";

	if (jobs.find(object) != jobs.end()) {
		for (auto &job : jobs[object]) {
			if (job->type == op) {
				sj = job.get();
				exists = true;
				break;
			}
		}
	} else {
		jobs[object] =
		    std::list<std::unique_ptr<Job>>(); /* create the entry */
	}

	if (!sj) /* must create a new job */
		sj = jobs[object]
			 .emplace_back(std::make_unique<Job>(object, op))
			 .get();

	if (goal_required)
		sj->goal_required = true;

	if (exists) /* deps will already have been added */
		return sj;

	if (among(op,
		{ JobType::kStart, JobType::kRestart, JobType::kTryRestart })) {
		object->foreach_edge(EdgeVisitor(Edge::kAddStart,
		    JobType::kStart, *this, sj, /* required */ true));
		object->foreach_edge(EdgeVisitor(Edge::kAddStartNonreq,
		    JobType::kStart, *this, sj, /* required */ false));
		object->foreach_edge(EdgeVisitor(Edge::kAddVerify,
		    JobType::kVerify, *this, sj, /* required */ true));
		object->foreach_edge(EdgeVisitor(Edge::kAddStop, JobType::kStop,
		    *this, sj, /* required */ true));
		object->foreach_edge(EdgeVisitor(Edge::kAddStopNonreq,
		    JobType::kStop, *this, sj, /* required */ false));
	} else if (op == JobType::kStop)
		object->foreach_edge(EdgeVisitor(Edge::kPropagatesStopTo,
		    JobType::kStop, *this, sj, /* required */ true));
	else if (among(op, { JobType::kReload, JobType::kTryReload }))
		object->foreach_edge(EdgeVisitor(Edge::kPropagatesReloadTo,
		    JobType::kTryReload, *this, sj, /* required */ true));

	if (among(op, { JobType::kRestart, JobType::kTryRestart }))
		object->foreach_edge(EdgeVisitor(Edge::kPropagatesRestartTo,
		    JobType::kTryRestart, *this, sj, /* required */ true));

	return sj;
}

#pragma endregion

#pragma region Visualisation

void
Transaction::Job::to_graph(std::ostream &out, bool edges) const
{
	std::string nodename = object->id().name + type_str(type);

	if (!edges) {
		out << nodename + "[label=\"" << *this << "\"];\n";
	} else {
		std::string nodename = object->id().name + type_str(type);
		for (auto &req : reqs) {
			std::string to_nodename = req->to->object->id().name +
			    type_str(req->to->type);
			out << nodename + " -> " + to_nodename + " ";
			out << "[label=\"req=" + std::to_string(req->required);
			out << ",goalreq=" +
				std::to_string(req->goal_required) + "\"]";
			out << ";\n";
		}
	}
}

void
Transaction::to_graph(std::ostream &out) const
{
	out << "digraph TX {\n";
	out << "graph [compound=true];\n";

	for (auto &obj : jobs) {
		out << "subgraph cluster_" + obj.first->id().name + " {\n";
		out << "label=\"" + obj.first->id().name + "\";\n";
		out << "color=lightgrey;\n";

		for (auto &job : obj.second)
			job->to_graph(out, false);

		out << "}\n"; /* terminate final subgraph */
	}

	/* now output edges */
	for (auto &pair : jobs)
		for (auto &job : pair.second)
			job->to_graph(out, true);

	out << "}\n";
}

#pragma endregion

const char *
Transaction::type_str(JobType type)
{
	static const char *const types[] = { [JobType::kStart] = "start",
		[JobType::kVerify] = "verify",
		[JobType::kStop] = "stop",
		[JobType::kReload] = "reload",
		[JobType::kRestart] = "restart",

		[JobType::kTryStart] = "try_restart",
		[JobType::kTryRestart] = "try_restart",
		[JobType::kTryReload] = "try_reload",
		[JobType::kReloadOrStart] = "reload_or_Start",
		[JobType::kRestartOrStart] = "restart_or_start" };
	return type == kInvalid ? "invalid" : types[type];
}