#ifndef SCHEDULER_RESTARTER_H_
#define SCHEDULER_RESTARTER_H_

#include "../scheduler/scheduler.h"

class Restarter {
    protected:
	Scheduler &sched;

	Restarter(Scheduler &sched)
	    : sched(sched) {};

    public:
	bool start_job(Transaction::Job::Id job, Transaction::JobType type);
	virtual bool start(Transaction::Job::Id obj) = 0;
	virtual bool stop(Transaction::Job::Id obj) = 0;
};

class TargetRestarter : public Restarter {
    public:
	bool start(Transaction::Job::Id obj);
	bool stop(Transaction::Job::Id obj);

	TargetRestarter(Scheduler &sched)
	    : Restarter(sched) {};
};

#endif /* SCHEDULER_RESTARTER_H_ */
