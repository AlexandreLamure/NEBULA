#ifndef TIMESTAMPQUERY_H
#define TIMESTAMPQUERY_H

#include <graphics.h>

#include <string>
#include <utility>

namespace nebula {

#define PROFILE_GPU(nameExpr) auto CREATE_UNIQUE_NAME_WITH_PREFIX(gpuProf) = ::nebula::ScopeGuard([zoneId = ::nebula::profile::beginProfileZone(nameExpr)] { ::nebula::profile::endProfileZone(zoneId); })


class TimestampQuery : NonCopyable {
    enum class State {
        None,
        Started,
        Ended,
        Resolved,
    };

    public:
        TimestampQuery() = default;
        ~TimestampQuery();

        TimestampQuery(TimestampQuery&& other);
        TimestampQuery& operator=(TimestampQuery&& other);

        void swap(TimestampQuery& other);

        static TimestampQuery createAndBegin();

        void begin();
        void end();

        Result<double> seconds(bool wait = false) const;

    private:
        void captureFromPool(u32 slot, const u64* ticks, u32 count);
        friend void resetTimestampQueries();

        u32 _begin = ~0u;
        u32 _end = ~0u;
        u32 _slot = 0;

        mutable double _time = 0.0;
        mutable State _state = State::None;
};



struct ProfileZone {
    std::string name;
    u32 containedZones = 0;
    float cpuTime = 0.0f;
    float gpuTime = 0.0f;
};

Span<ProfileZone> retrieveProfile();
void processProfileMarkers();


namespace profile {
    u32 beginProfileZone(const char* name);
    void endProfileZone(u32 zoneId);

    void destroyProfile();
}

}

#endif // TIMESTAMPQUERY_H
