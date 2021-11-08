#include <algorithm>
#include <iostream>

#include "scheduler.h"

Transaction::Transaction(Scheduler &sched, Schedulable::SPtr object, JobType op)
    : sched(sched)
{
	objective = submit_job(object, op, true);
	to_graph(std::cout);
	if (!verify_acyclic())
		throw("Transaction is unresolveably cyclical");
	to_graph(std::cout);
}

void
Transaction::Job::get_del_list(std::vector<Job *> &dellist)
{
	dellist.push_back(this);
	for (auto &req_on : reqs_on) {
		if (req_on->required) {
			req_on->from->get_del_list(dellist);
		}
	}
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

/** Delete all jobs on \p object. */
void
Transaction::del_jobs_for(Schedulable::SPtr object)
{
	std::vector<Job *> dellist;

	for (auto rnge = jobs.equal_range(object); rnge.first != rnge.second;
	     rnge.first++) {
		auto &job = rnge.first->second;

		job->get_del_list(dellist);
	}

	for (auto job : dellist)
		multimap_erase_if(jobs, UniquePtrEq<Job>(job));
}

Transaction::Job *
Transaction::job_for(Schedulable::SPtr object)
{
	auto it = jobs.find(object);
	return it == jobs.end() ? nullptr : it->second.get();
}

Transaction::Job *
Transaction::job_for(ObjectId id)
{
	for (auto &pair : jobs) {
		if (id == pair.first)
			return pair.second.get();
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
		if (tx.job_for(edge->to) != nullptr) {
			/* job(s) exist for this After edge - check for loop */
			if (tx.is_cyclic(tx.sched.object_get(edge->to)
					     ->shared_from_this(), // fixme ugly
				path))
				cyclic = true;
		}
	}
}

bool
Transaction::is_cyclic(Schedulable::SPtr job,
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
Transaction::object_jobs_required(Schedulable::SPtr object)
{
	for (auto rnge = jobs.equal_range(object); rnge.first != rnge.second;
	     rnge.first++) {
		auto job = rnge.first->second.get();

		if (job == objective) {
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

		if (!object_jobs_required(job)) {
			std::cout << "Cycle resolved: deleting jobs on "
				  << job->id().name
				  << " as non-essential to goal.\n";
			del_jobs_for(job);
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
		if (is_cyclic(job.second->object, path)) {
			printf("CYCLE DETECTED:\n");
			for (auto &obj : path)
				printf("%s -> ", obj->id().name.c_str());
			printf("%s\n", path.front()->id().name.c_str());
			if (try_remove_cycle(path))
				goto restart; /* iterator invalidated */
			else
				return false;
		}
	}
	return true;
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
		Transaction::Job *sj =
		    tx.submit_job(tx.sched.object_get(edge->to)
				      ->shared_from_this() // fixme ugly
			,
			op, goal_required);

		requirer->add_req(sj, is_required, goal_required);
	}
}

Transaction::Job *
Transaction::submit_job(Schedulable::SPtr object, JobType op,
    bool goal_required)
{
	Job *sj = NULL;	     /* newly created or existing subjob */
	bool exists = false; /* whether the subjob already exists */

	if (jobs.find(object) != jobs.end()) {
		for (auto rnge = jobs.equal_range(object);
		     rnge.first != rnge.second; rnge.first++) {
			if (rnge.second->second->type == op) {
				sj = rnge.second->second.get();
				break;
			}
		}
	}

	if (!sj) /* must create a new job */
	{
		sj = jobs.insert(std::make_pair(object,
				     std::make_unique<Job>(object, op)))
			 ->second.get();
	}

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
	Schedulable *obj = nullptr;
	out << "digraph TX {\n";
	out << "graph [compound=true];\n";

	for (auto &job : jobs) {
		if (job.first.get() != obj) {
			if (obj != NULL)
				out << "}\n"; /* terminate previous subgraph */

			obj = job.first.get();
			out << "subgraph cluster_" + obj->id().name + " {\n";
			out << "label=\"" + obj->id().name + "\";\n";
			out << "color=lightgrey;\n";
		}

		job.second->to_graph(out, false);
	}
	out << "}\n"; /* terminate final subgraph */

	/* now output edges */
	for (auto &job : jobs)
		job.second->to_graph(out, true);

	out << "}\n";
}

#pragma endregion

const char *
Transaction::type_str(JobType type)
{
	static const char *const types[] = {
		[JobType::kStart] = "start",
		[JobType::kVerify] = "verify",
		[JobType::kStop] = "stop",
		[JobType::kReload] = "reload",
		[JobType::kRestart] = "restart",

		[JobType::kTryStart] = "try_restart",
		[JobType::kTryRestart] = "try_restart",
		[JobType::kTryReload] = "try_reload",
		[JobType::kReloadOrStart] = "reload_or_Start",
	};
	return types[type];
}