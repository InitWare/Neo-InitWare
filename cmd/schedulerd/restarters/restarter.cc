#include "restarter.h"

bool
TargetRestarter::start(Transaction::Job::Id id)
{
	sched.job_complete(id, Transaction::Job::kSuccess);
	// sched.object_set_state(, Schedulable::State state)
	return 0;
}

bool
TargetRestarter::stop(Transaction::Job::Id id)
{
	sched.job_complete(id, Transaction::Job::kSuccess);
	return 0;
}
