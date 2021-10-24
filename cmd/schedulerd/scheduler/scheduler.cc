#include "scheduler.h"

Edge *
Schedulable::add_edge(Edge::Type type, SPtr to)
{
	Edge *edge;

	edges.emplace_back(new Edge({ type, this, to }));
}
