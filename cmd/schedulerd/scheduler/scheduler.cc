#include "scheduler.h"

Edge::Edge(Type type, Schedulable::WPtr from, Schedulable::SPtr to)
    : type(type)
    , from(from)
    , to(to)
{
	to->edges_to.push_back(this);
}

Edge::~Edge()
{
	to->edges_to.remove(this);
}

Edge *
Schedulable::add_edge(Edge::Type type, SPtr to)
{
	edges.emplace_back(new Edge(type, shared_from_this(), to));
	return edges.back().get();
}

Schedulable::SPtr
Scheduler::add_object(Schedulable::SPtr obj)
{
	objects.emplace_back(obj);
	return obj;
}