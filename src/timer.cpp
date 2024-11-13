#include "timer.hpp"

void Timer::tick()
{
	m_last = m_current;
	m_current = Clock::now();
}

void Timer::reset()
{
	m_current = Clock::now();
	m_last = m_current;
}

double Timer::deltaTimeMS()
{
	Duration delta = m_current - m_last;
	return delta.count();
}

double Timer::timeSinceStartMS()
{
	Duration delta = m_current - m_start;
	return delta.count();
}
