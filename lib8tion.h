#include <chrono>
#include <ctime>

using namespace std;

/**
 * Front FastLED
 */
class CEveryNMillis
{
public:
    long mPrevTrigger;
    long mPeriod;

    CEveryNMillis()
    {
        reset();
        mPeriod = 1;
    };

    CEveryNMillis(long period)
    {
        reset();
        setPeriod(period);
    };

    void setPeriod(long period)
    {
        mPeriod = period;
    };
    long getTime()
    {
        return chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
    };
    long getPeriod()
    {
        return mPeriod;
    };
    long getElapsed()
    {
        return getTime() - mPrevTrigger;
    }
    long getRemaining()
    {
        return mPeriod - getElapsed();
    }
    long getLastTriggerTime()
    {
        return mPrevTrigger;
    }
    bool ready()
    {
        bool isReady = (getElapsed() >= mPeriod);
        if (isReady)
        {
            reset();
        }
        return isReady;
    }
    void reset()
    {
        mPrevTrigger = getTime();
    };
    void trigger()
    {
        mPrevTrigger = getTime() - mPeriod;
    };

    operator bool()
    {
        return ready();
    }
};

#define CONCAT_HELPER(x, y) x##y
#define CONCAT_MACRO(x, y) CONCAT_HELPER(x, y)
#define EVERY_N_MILLIS(N) EVERY_N_MILLIS_I(CONCAT_MACRO(PER, __COUNTER__), N)
#define EVERY_N_MILLIS_I(NAME, N) static CEveryNMillis NAME(N); if (NAME)
