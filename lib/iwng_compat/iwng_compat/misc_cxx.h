/*
 * Miscellaneous utilities for C++
 */

#ifndef MISC_CXX_H_
#define MISC_CXX_H_

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
