#include "scheduler.h"

Transaction::Transaction(Schedulable::SPtr object, Job::Type op)
{
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
				sj = subjob.get();
				exists = true;
			}
		}

		if (!sj)
			job->subjobs.emplace_back(
			    std::make_unique<Job::Subjob>(job.get(), op));
	}

	if (!sj)
		jobs[object] = std::make_unique<Job>(object, op, &sj);

	if (requirer)
		requirer->add_req(sj, true);
}