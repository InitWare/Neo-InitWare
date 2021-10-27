#include <algorithm>

#include "scheduler.h"

class EdgeVisitor : public Edge::Visitor {
    public:
	EdgeVisitor(Edge::Type type, Transaction::Job::Type op, Transaction &tx,
	    Transaction::Job::Subjob *requirer)
	    : type(type)
	    , op(op)
	    , tx(tx)
	    , requirer(requirer)
	{
	}

	void operator()(std::unique_ptr<Edge> &edge);

    private:
	Edge::Type type;
	Transaction::Job::Type op;
	Transaction &tx;
	Transaction::Job::Subjob *requirer;
};

void
EdgeVisitor::operator()(std::unique_ptr<Edge> &edge)
{
	if (edge->type & type)
		tx.add_job_and_deps(edge->to, op, requirer);
}

template <typename T>
bool
among(const T &variable, std::initializer_list<T> values)
{
	return (std::find(std::begin(values), std::end(values), variable) !=
	    std::end(values));
}

Transaction::Transaction(Schedulable::SPtr object, Job::Type op)
{
	objective = add_job_and_deps(object, op, NULL);
}

void
Transaction::Job::Subjob::add_req(Subjob *on, bool required)
{
	reqs.emplace_back(std::make_unique<Requirement>(on, required));
	on->reqs_on.emplace_back(reqs.back().get());
}

Transaction::Job::Requirement::~Requirement()
{
	on->reqs_on.remove(this);
}

Transaction::Job::Subjob *
Transaction::add_job_and_deps(Schedulable::SPtr object, Job::Type op,
    Job::Subjob *requirer)
{
	Job::Subjob *sj;     /* newly created or existing subjob */
	bool exists = false; /* whether the subjob already exists */

	if (jobs.find(object) != jobs.end()) {
		std::unique_ptr<Job> &job = jobs[object];

		for (auto &subjob : job->subjobs) {
			if (subjob->type == op) {
				sj = subjob.get(); /* use existing subjob */
				exists = true;
			}
		}

		if (!sj) /* create new subjob of existing job */
			job->subjobs.emplace_back(
			    std::make_unique<Job::Subjob>(job.get(), op));
	}

	if (!sj) /* create a new job with subjob */
		jobs[object] = std::make_unique<Job>(object, op, &sj);

	if (requirer) /* add req from requirer to this */
		requirer->add_req(sj, true);

	if (exists) /* deps will already have been added */
		return sj;

	if (among(op, { Job::kStart, Job::kRestart })) {
		object->foreach_edge(
		    EdgeVisitor(Edge::kRequire, Job::kStart, *this, sj));
		object->foreach_edge(
		    EdgeVisitor(Edge::kWant, Job::kStart, *this, sj));
		object->foreach_edge(
		    EdgeVisitor(Edge::kRequisite, Job::kVerify, *this, sj));
		object->foreach_edge(
		    EdgeVisitor(Edge::kConflict, Job::kStop, *this, sj));
		object->foreach_edge(
		    EdgeVisitor(Edge::kConflictedBy, Job::kStop, *this, sj));
	}

	if (among(op, { Job::kStop, Job::kRestart })) {
	}

	return sj;
}
