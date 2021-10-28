
#include <iostream>

#include "scheduler.h"

std::ostream &
Transaction::Job::Subjob::print(std::ostream &os) const
{
	os << job->object->m_name << "/" << type_str(type);
	return os;
}

Transaction::Job::Requirement::~Requirement()
{
	to->reqs_on.remove(this);
}