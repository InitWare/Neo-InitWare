#ifndef SCHEDULER_RESTARTER_H_
#define SCHEDULER_RESTARTER_H_

#include "../scheduler/scheduler.h"

class Restarter {
    public:
	bool start_job(Schedulable::SPtr &obj, Transaction::JobType type);
	virtual bool start(Schedulable::SPtr &obj) = 0;
	virtual bool stop(Schedulable::SPtr &obj) = 0;
};

#endif /* SCHEDULER_RESTARTER_H_ */
