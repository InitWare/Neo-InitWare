#ifndef JS_RESTARTER_H_
#define JS_RESTARTER_H_

#include "../scheduler/restarter.h"

class JSRestarter : public Restarter {
	Scheduler &m_sched;
};

#endif /* JS_RESTARTER_H_ */
