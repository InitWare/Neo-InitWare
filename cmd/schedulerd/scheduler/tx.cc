
#include <iostream>

#include "scheduler.h"

std::ostream &
Transaction::Job::print(std::ostream &os) const
{
	os << object->m_name << "/" << type_str(type);
	return os;
}
