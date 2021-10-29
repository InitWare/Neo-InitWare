/*
 * Miscellaneous utilities for C++
 */

#ifndef MISC_CXX_H_
#define MISC_CXX_H_

#include <algorithm>
#include <map>
#include <memory>

/** Functor to check if an std::unique_ptr<T> is equal to a pointer val. */
template <typename T> class UniquePtrEq {
	T *val;

    public:
	UniquePtrEq(T *val)
	    : val(val)
	{
	}

	bool operator()(std::unique_ptr<T> &ptr);
};

/** To iterate over a container in reverse. */
template <typename T> struct Reverse {
	T &iterable;
};

/** To allow a class to be printable to an ostream via a print() member. */
template <typename T> class Printable {
	friend std::ostream &operator<<(std::ostream &os, const T &t)
	{
		return t.print(os);
	};
};

/** To check whether a value is one of a set of values. */
template <typename T>
bool
among(const T &variable, std::initializer_list<T> values)
{
	return (std::find(std::begin(values), std::end(values), variable) !=
	    std::end(values));
}

/** To erase from a multimap if a predicate is satisfied. */
template <typename MapT, typename PredT>
void
multimap_erase_if(MapT &map, PredT eq)
{
	for (auto it = map.begin(); it != map.end();) {
		if (eq(it->second))
			it = map.erase(it);
		else
			it++;
	}
}

/* template implementations */
template <typename T>
bool
UniquePtrEq<T>::operator()(std::unique_ptr<T> &ptr)
{
	if (ptr.get() == val)
		return true;
	else
		return false;
}

template <typename T>
auto
begin(Reverse<T> w)
{
	return std::rbegin(w.iterable);
}

template <typename T>
auto
end(Reverse<T> w)
{
	return std::rend(w.iterable);
}

template <typename T>
Reverse<T>
reverse(T &&iterable)
{
	return { iterable };
}

#endif /* MISC_CXX_H_ */
