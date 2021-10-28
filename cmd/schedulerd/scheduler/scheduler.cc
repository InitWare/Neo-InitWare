#include <iostream>

#include "scheduler.h"

std::string
Transaction::Job::describe() const
{
	std::string res = "on " + object->m_name + " do {";
	for (auto &subjob : subjobs)
		res += " " + std::to_string(subjob->type);
	res += " }";
	return std::move(res);
}

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

/* Graph output */
void
Edge::to_graph(std::ostream &out) const
{
	auto p = from.lock();
	out << p->m_name << " -> " << to->m_name;
	out << "[label=\"" << type_str() << "\"];\n";
}

void
Schedulable::to_graph(std::ostream &out) const
{
	out << m_name + ";\n";
	for (auto &edge : edges_to)
		edge->to_graph(out);
}

void
Scheduler::to_graph(std::ostream &out) const
{
	out << "digraph sched {\n";
	for (auto object : objects)
		object->to_graph(out);
	out << "}\n";
}

std::string
Edge::type_str() const
{
	/* clang-format off */
	static const std::map<int, const char *> types = {
		{kRequire, "Require"},
		{kWant, "Want"},
		{kRequisite, "Requisite"},
		{kConflict, "Conflict"},
		{kConflictedBy, "ConflictedBy"},
		{kPropagatesStopTo, "PropagatesStopTo"},
		{kPropagatesRestartTo, "PropagatesRestartTo"},
		{kPropagatesReloadTo, "PropagatesReloadTo"},
		{kStartOnStarted, "StartOnStarted"},
		{kTryStartOnStarted, "TryStartOnSTaretd"},
		{kStopOnStarted, "StopOnStarted"},
		{kBoundBy, "BoundBy"},
		{kOnSuccess, "OnSuccess"},
		{kOnFailure, "OnFailure"},
		{kAfter, "After"},
		{kBefore, "Before"}};
	/* clang-format on */
	std::string res;
	bool first = true;

	for (auto &entry : types) {
		if (type & entry.first) {
			if (!first)
				res += "\\n";
			res += entry.second;
			first = false;
		}
	}

	return std::move(res);
}