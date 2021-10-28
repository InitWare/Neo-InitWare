
#include "scheduler.h"

Transaction::Job::Requirement::~Requirement()
{
	to->reqs_on.remove(this);
}