#pragma

#include <chrono>

class Timer
{
public:
	using Clock = std::chrono::steady_clock;
	using Duration = std::chrono::duration<double, std::milli>;
	using TimePoint = std::chrono::time_point<Clock, Duration>;

	void tick();

	void reset();

	double deltaTimeMS();

	double timeSinceStartMS();

private:
	TimePoint m_start = Clock::now();
	TimePoint m_current = m_start;
	TimePoint m_last = m_current;
};
