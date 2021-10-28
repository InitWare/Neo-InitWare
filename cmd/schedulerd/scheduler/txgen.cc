#include <algorithm>
#include <iostream>

#include "scheduler.h"

template <typename T> struct reversify {
	T &iterable;
};

template <typename T>
auto
begin(reversify<T> w)
{
	return std::rbegin(w.iterable);
}

template <typename T>
auto
end(reversify<T> w)
{
	return std::rend(w.iterable);
}

template <typename T>
reversify<T>
reverse(T &&iterable)
{
	return { iterable };
}

class EdgeVisitor : public Edge::Visitor {
    public:
	EdgeVisitor(Edge::Type type, Transaction::JobType op, Transaction &tx,
	    Transaction::Job::Subjob *requirer, bool is_required)
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
	Transaction::Job::Subjob *requirer;
	bool is_required;
};

void
EdgeVisitor::operator()(std::unique_ptr<Edge> &edge)
{
	if (edge->type & type) {
		Transaction::Job::Subjob *sj = tx.submit_job(edge->to, op);
		requirer->add_req(sj, is_required,
		    (tx.objective == requirer) && is_required);
	}
}

template <typename T>
bool
among(const T &variable, std::initializer_list<T> values)
{
	return (std::find(std::begin(values), std::end(values), variable) !=
	    std::end(values));
}

Transaction::Transaction(Schedulable::SPtr object, JobType op)
{
	submit_job(object, op, true);
	verify_acyclic();
	to_graph(std::cout);
}

void
Transaction::Job::get_del_list(std::vector<Subjob *> &dellist)
{
	if (deleting)
		return;

	deleting = true;

	/* every subjob is deleted */
	for (auto &subjob : subjobs) {
		dellist.push_back(subjob.get());
		for (auto &req_on : subjob->reqs_on) {
			if (req_on->required) {
				req_on->from->get_del_list(dellist);
			}
		}
	}
}

void
Transaction::Job::Subjob::get_del_list(std::vector<Subjob *> &dellist)
{
	dellist.push_back(this);
	for (auto &req_on : reqs_on) {
		if (req_on->required) {
			req_on->from->get_del_list(dellist);
		}
	}
}

Transaction::Job::Subjob::~Subjob()
{
	/** remove all requirements on this subjob from others' reqs */
	for (auto req_on = reqs_on.begin(); req_on != reqs_on.end();) {
		auto old = *req_on;
		req_on = reqs_on.erase(req_on);
		old->from->del_req(old);
	}
}

void
Transaction::Job::Subjob::add_req(Subjob *on, bool required, bool goal_required)
{
	reqs.emplace_back(
	    std::make_unique<Requirement>(this, on, required, goal_required));
	on->reqs_on.emplace_back(reqs.back().get());
}

void
Transaction::Job::Subjob::del_req(Requirement *reqp)
{
	for (auto &req : reqs) {
		if (req.get() == reqp) {
			reqs.remove(req);
			return;
		}
	}
	throw("Requirement not found");
}

/*
 * loop detection
 */

class OrderVisitor : public Edge::Visitor {
    public:
	OrderVisitor(Transaction &tx, std::vector<Transaction::Job *> &path,
	    bool &cyclic)
	    : tx(tx)
	    , path(path)
	    , cyclic(cyclic) {};

	void operator()(std::unique_ptr<Edge> &edge);

    private:
	Transaction &tx;
	std::vector<Transaction::Job *> &path;
	bool &cyclic;
};

void
OrderVisitor::operator()(std::unique_ptr<Edge> &edge)
{
	if (edge->type == Edge::kAfter && !cyclic) {
		if (tx.jobs.find(edge->to) != tx.jobs.end()) {
			/* a job exists for this After edge - check for loop */
			if (tx.is_cyclic(tx.jobs[edge->to].get(), path))
				cyclic = true;
		}
	}
}

bool
Transaction::is_cyclic(Job *job, std::vector<Job *> &path)
{
	bool cyclic;

	if (std::find(path.begin(), path.end(), job) != path.end())
		return true;

	path.push_back(job);
	job->object->foreach_edge(OrderVisitor(*this, path, cyclic));
	if (!cyclic)
		path.pop_back();

	return cyclic;
}

bool
Transaction::try_remove_cycle(std::vector<Job *> &path)
{
	for (auto &job : reverse(path)) {
		bool essential = false;

		if (job == objective->job) {
			printf("Not deleting job on %s as it's the goal\n",
			    job->object->m_name.c_str());
			continue;
		}

		for (auto &subjob : job->subjobs) {
			for (auto req_on : subjob->reqs_on) {
				if (req_on->goal_required) {
					essential = true;
					break;
				}
			}

			if (essential)
				break;
		}

		if (essential) {
			printf("Can't delete jobs on %s as essential to goal\n",
			    job->object->m_name.c_str());
		} else {
			std::vector<Job::Subjob *> dellist;

			printf(
			    "Can delete jobs on %s as nonessential to goal\n",
			    job->object->m_name.c_str());
			job->get_del_list(dellist);

			for (auto job : dellist) {
				printf("May delete subjob %s/%s\n",
				    job->job->object->m_name.c_str(),
				    type_str(job->type));
			}
		}

		/* delete all jobs transitively requiring it */
	}

	return false;
}

bool
Transaction::verify_acyclic()
{
	for (auto &job : jobs) {
		std::vector<Transaction::Job *> path;
		if (is_cyclic(job.second.get(), path)) {
			printf("CYCLE DETECTED:\n");
			for (auto &job2 : path) {
				printf("%s -> ", job2->object->m_name.c_str());
			}
			printf("%s\n", path.front()->object->m_name.c_str());
			try_remove_cycle(path);
			return true;
		}
	}
	return false;
}

Transaction::Job::Subjob *
Transaction::submit_job(Schedulable::SPtr object, JobType op, bool is_goal)
{
	Job::Subjob *sj = NULL; /* newly created or existing subjob */
	bool exists = false;	/* whether the subjob already exists */

	if (jobs.find(object) != jobs.end()) {
		std::unique_ptr<Job> &job = jobs[object];

		for (auto &subjob : job->subjobs) {
			if (subjob->type == op) {
				sj = subjob.get(); /* use existing
						      subjob */
				exists = true;
			}
		}

		if (!sj) /* create new subjob of existing job */
			job->subjobs.emplace_back(
			    std::make_unique<Job::Subjob>(job.get(), op));
	}

	if (!sj) /* create a new job with subjob */
		jobs[object] = std::make_unique<Job>(object, op, &sj);

	if (is_goal)
		objective = sj;

	if (exists) /* deps will already have been added */
		return sj;

	if (among(op,
		{ JobType::kStart, JobType::kRestart, JobType::kTryRestart })) {
		object->foreach_edge(EdgeVisitor(Edge::kRequire,
		    JobType::kStart, *this, sj, /* required */ true));
		object->foreach_edge(EdgeVisitor(Edge::kWant, JobType::kStart,
		    *this, sj, /* required */ false));
		object->foreach_edge(EdgeVisitor(Edge::kRequisite,
		    JobType::kVerify, *this, sj, /* required */ true));
		object->foreach_edge(EdgeVisitor(Edge::kConflict,
		    JobType::kStop, *this, sj, /* required */ true));
		object->foreach_edge(EdgeVisitor(Edge::kConflictedBy,
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

void
Transaction::Job::to_graph(std::ostream &out) const
{
	out << "subgraph cluster_" + object->m_name + " {\n";
	out << "label=\"" + object->m_name + "\";\n";
	out << "color=lightgrey;\n";
	for (auto &sub : subjobs) {
		std::string nodename = object->m_name + type_str(sub->type);
		out << nodename + "[label=\"" + type_str(sub->type) + "\"];\n";
	}
	out << "}\n";

	for (auto &sub : subjobs) {
		std::string nodename = object->m_name + type_str(sub->type);
		for (auto &req : sub->reqs) {
			std::string to_nodename = req->to->job->object->m_name +
			    type_str(req->to->type);
			out << nodename + " -> " + to_nodename + " ";
			out << "[label=\"req=" + std::to_string(req->required);
			out << ",goalreq=" +
				std::to_string(req->goal_required) + "\"]";
			out << ";\n";
		}
	}

	out << "\n";
}

void
Transaction::to_graph(std::ostream &out) const
{
	out << "digraph TX {\n";
	out << "graph [compound=true];\n";
	for (auto &job : jobs)
		job.second->to_graph(out);
	out << "}\n";
}

const char *
Transaction::type_str(JobType type)
{
	static const char *const types[] = {
		[JobType::kStart] = "start",
		[JobType::kVerify] = "verify",
		[JobType::kStop] = "stop",
		[JobType::kReload] = "reload",
		[JobType::kRestart] = "restart",

		[JobType::kTryRestart] = "try_restart",
		[JobType::kTryReload] = "try_reload",
		[JobType::kReloadOrStart] = "reload_or_Start",
	};

	return types[type];
}