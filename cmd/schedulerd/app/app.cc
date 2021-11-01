#include <sys/event.h>

#include <system_error>

#include "app.h"

App::timerid_t
App::add_timer(bool recur, int ms, Timer::callback_t cb, uintptr_t udata)
{
	Timer *timer;
	struct kevent kev;
	int ret;

	m_timers.emplace_back(std::make_unique<Timer>(cb, udata));
	timer = m_timers.back().get();

	EV_SET(&kev, (uintptr_t)timer, EVFILT_TIMER,
	    EV_ADD | EV_ENABLE | (recur ? 0 : EV_ONESHOT), 0, ms, NULL);
	ret = kevent(m_kq, &kev, 1, NULL, 0, NULL);
	if (ret < 0) {
		m_timers.pop_back();
		throw std::system_error(errno, std::generic_category());
	}

	log_trace("Added timer %lu\n", (timerid_t)timer);

	return (timerid_t)timer;
}

int
App::del_timer(timerid_t id)
{
	struct kevent kev;
	int ret;

	for (auto &timer : m_timers) {
		if ((uintptr_t)timer.get() == id) {
			EV_SET(&kev, id, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
			ret = kevent(m_kq, &kev, 1, NULL, 0, NULL);
			if (ret < 0)
				log_dbg("Couldn't remove KQueue timer!");

			m_timers.remove(timer);
			log_trace("Deleted timer %lu\n", id);
			return 0;
		}
	}

	log_dbg("Couldn't find timer of that ID\n");
	return -ENOENT;
}

int
App::add_fd(int fd, int events, FD::callback_t cb)
{
	FD *fdo;
	struct kevent kev;
	int ret;

	m_fds.emplace_back(std::make_unique<FD>(fd, events, cb));
	fdo = m_fds.back().get();

	/* we need to use typeof() as libkqueue and BSD kqueue vary */
	EV_SET(&kev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0,
	    (typeof(kev.udata))fdo);
	ret = kevent(m_kq, &kev, 1, NULL, 0, NULL);
	if (ret < 0) {
		m_fds.pop_back();
		throw std::system_error(errno, std::generic_category());
	}

	log_trace("Added FD %d\n", fd);

	return 0;
}

int
App::del_fd(int fd)
{
	struct kevent kev;
	int ret;

	for (auto &fdo : m_fds) {
		if (fdo->m_fd == fd) {
			EV_SET(&kev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
			ret = kevent(m_kq, &kev, 1, NULL, 0, NULL);
			if (ret < 0)
				log_dbg("Couldn't remove KQueue FD event!");

			m_fds.remove(fdo);
			log_trace("Deleted FD %d\n", fd);
			return 0;
		}
	}

	log_dbg("Asked to delete watch on FD %d but none exists\n", fd);
	return -ENOENT;
}

void
App::handle_timer(struct kevent *kev)
{
	Timer *timer = (Timer *)kev->ident;

	log_trace("Timer %lu elapsed\n", timer);
	timer->m_cb((timerid_t)timer, timer->m_udata);
}

void
App::handle_fd(struct kevent *kev)
{
	FD *fd = (FD *)kev->udata;

	log_trace("FD %d had an event\n", kev->ident);
	fd->m_cb(kev->ident);
}

int
App::loop()
{
	struct kevent rev;
	int ret;

	while (true) {
		log_trace(" -- iteration --\n");
		ret = kevent(m_kq, NULL, 0, &rev, 1, NULL);
		if (ret < 0)
			log_err("KEvent returned %d: %m", ret);
		else if (ret == 0)
			log_dbg("KEvent returned 0\n");
		else {
			switch (rev.filter) {
			case EVFILT_TIMER:
				handle_timer(&rev);
				break;
			case EVFILT_READ:
				handle_fd(&rev);
				break;
			default:
				log_err("Unhandled KEvent filter!\n");
			}
		}
		m_js.run_pending_jobs();
	}

	return 0;
}

App::App()
    : m_js(this)
    , m_sched(*this)
{
	m_kq = kqueue();
}
