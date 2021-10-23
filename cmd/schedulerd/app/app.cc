#include <sys/event.h>

#include <system_error>

#include "app.h"

App::timerid_t
App::add_timer(bool recur, int ms, Timer::callback_t cb)
{
	Timer *timer;
	struct kevent kev;
	int ret;

	m_timers.emplace_back(std::make_unique<Timer>(cb));
	timer = m_timers.back().get();

	EV_SET(&kev, (uintptr_t)timer, EVFILT_TIMER,
	    EV_ADD | EV_ENABLE | (recur ? 0 : EV_ONESHOT), 0, ms, NULL);
	ret = kevent(kq, &kev, 1, NULL, 0, NULL);
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
			ret = kevent(kq, &kev, 1, NULL, 0, NULL);
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

void
App::handle_timer(struct kevent *kev)
{
	Timer *timer = (Timer *)kev->ident;

	log_trace("Timer %lu elapsed\n", timer);
	timer->m_cb((timerid_t)timer);
}

int
App::loop()
{
	struct kevent rev;
	int ret;

	while (true) {
		log_trace(" -- iteration --\n");
		ret = kevent(kq, NULL, 0, &rev, 1, NULL);
		if (ret < 0)
			log_err("KEvent returned %d: %m", ret);
		else if (ret == 0)
			log_dbg("KEvent returned 0\n");
		else {
			switch (rev.filter) {
			case EVFILT_TIMER:
				handle_timer(&rev);
				break;
			default:
				log_err("Unhandled KEvent filter!\n");
			}
		}
		log_trace("Running pending jobs:\n");
		js.run_pending_jobs();
	}

	return 0;
}

App::App()
    : js(this)
{
	kq = kqueue();
}
