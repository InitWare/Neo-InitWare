
#include <iostream>

#include "scheduler.h"

std::ostream &
Transaction::Job::print(std::ostream &os) const
{
	os << object->id.name << "/" << type_str(type);
	return os;
}
