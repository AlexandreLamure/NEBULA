#include "TimestampQuery.h"

#include "VkContext.h"

#include <string>
#include <deque>
#include <vector>
#include <array>

#include <volk.h>

namespace nebula {

namespace profile {
    struct Marker {
        std::string name;
        u32 containedZones;
        double cpuTime;
        TimestampQuery query;
    };

    static std::vector<Marker> currentFrame;
    static std::deque<std::vector<Marker>> queuedFrames;
    static std::vector<ProfileZone> ready;

    void destroyProfile() {
        currentFrame.clear();
        queuedFrames.clear();
        ready.clear();
    }

    u32 beginProfileZone(const char* name) {
        const u32 index = u32(currentFrame.size());

        // Either you forgot to call processProfileMarkers every frame, or you have too many marker
        // In the later case, you can just remove this assert
        ALWAYS_ASSERT(index < 65536, "Too many profile markers");

        Marker& marker = currentFrame.emplace_back();
        marker.name = name;
        marker.cpuTime = programTime();
        marker.query.begin();

        return index;
    }

    // containedZones is how many markers nested inside this one, used to draw a tree in the profiler UI.
    void endProfileZone(u32 zoneId) {
        Marker& marker = currentFrame[zoneId];
        marker.cpuTime = programTime() - marker.cpuTime;
        marker.containedZones = u32(currentFrame.size()) - zoneId - 1;
        marker.query.end();
    }
}




// GPU timestamps lag the CPU, so frames sit in a queue until the oldest one's queries are ready.
void processProfileMarkers() {
    profile::queuedFrames.emplace_back().swap(profile::currentFrame);
    DEBUG_ASSERT(profile::currentFrame.empty());

    bool anyProfileReady = false;
    std::vector<profile::Marker> readyFrame;
    while(!profile::queuedFrames.empty()) {
        auto& frame = profile::queuedFrames.front();

        bool ready = true;
        for(auto& marker : frame) {
            if(!marker.query.seconds().isOk) {
                ready = false;
                break;
            }
        }

        if(ready) {
            anyProfileReady = true;
            readyFrame = std::move(frame);
            profile::queuedFrames.pop_front();
        } else {
            break;
        }
    }

    if(anyProfileReady) {
        profile::ready.clear();
        for(auto& marker : readyFrame) {
            ProfileZone& zone = profile::ready.emplace_back();
            zone.name = std::move(marker.name);
            zone.containedZones = marker.containedZones;
            zone.cpuTime = float(marker.cpuTime);
            zone.gpuTime = float(marker.query.seconds(true).value);
        }
    }
}

Span<ProfileZone> retrieveProfile() {
    return profile::ready;
}

void resetTimestampQueries() {
    GraphicsContext& c = ctx();
    const u32 slot = c.frameIndex;
    VkQueryPool pool = c.timestampPools[slot];
    if(!pool) {
        return;
    }

    const u32 count = c.timestampAllocated[slot];
    if(count) {
        std::array<u64, timestampQueriesPerFrame> ticks{};
        vkCheck(vkGetQueryPoolResults(
            c.device,
            pool,
            0,
            count,
            count * sizeof(u64),
            ticks.data(),
            sizeof(u64),
            VK_QUERY_RESULT_64_BIT
        ));

        for(auto& marker : profile::currentFrame) {
            marker.query.captureFromPool(slot, ticks.data(), count);
        }
        for(auto& frame : profile::queuedFrames) {
            for(auto& marker : frame) {
                marker.query.captureFromPool(slot, ticks.data(), count);
            }
        }
    }

    vkResetQueryPool(c.device, pool, 0, timestampQueriesPerFrame);
    c.timestampAllocated[slot] = 0;
}

// Some queues report fewer than 64 valid timestamp bits; wrap arithmetic must mask to that width.
static u64 timestampMask() {
    const u32 bits = ctx().timestampValidBits;
    if(bits == 0 || bits >= 64) {
        return ~u64(0);
    }
    return (u64(1) << bits) - 1;
}

// Wrap-safe tick delta times timestampPeriod (nanoseconds) to seconds.
static double ticksToSeconds(u64 beginTicks, u64 endTicks) {
    const u64 delta = (endTicks - beginTicks) & timestampMask();
    return double(delta) * double(ctx().timestampPeriod) * 1e-9;
}

TimestampQuery::~TimestampQuery() {
    DEBUG_ASSERT(_state ==  State::Resolved || _state == State::Ended || _state == State::None);
}

TimestampQuery::TimestampQuery(TimestampQuery&& other) {
    swap(other);
}

TimestampQuery& TimestampQuery::operator=(TimestampQuery&& other) {
    swap(other);
    return *this;
}

void TimestampQuery::swap(TimestampQuery& other) {
    std::swap(_begin, other._begin);
    std::swap(_end, other._end);
    std::swap(_slot, other._slot);
    std::swap(_time, other._time);
    std::swap(_state, other._state);
}

TimestampQuery TimestampQuery::createAndBegin() {
    TimestampQuery ts;
    ts.begin();
    return ts;
}

void TimestampQuery::begin() {
    DEBUG_ASSERT(_state == State::None);
    _state = State::Started;

    if(!vkIsRecording()) {
        return;
    }

    GraphicsContext& c = ctx();
    _slot = c.frameIndex;
    VkQueryPool pool = c.timestampPools[_slot];
    if(!pool) {
        return;
    }

    ALWAYS_ASSERT(c.timestampAllocated[_slot] + 2 <= timestampQueriesPerFrame, "Too many GPU timestamps this frame");
    _begin = c.timestampAllocated[_slot];
    _end = _begin + 1;
    c.timestampAllocated[_slot] += 2;

    vkCmdWriteTimestamp2(vkCommandBuffer(), VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, pool, _begin);
}

void TimestampQuery::end() {
    DEBUG_ASSERT(_state == State::Started);
    _state = State::Ended;

    if(_begin == ~0u) {
        return;
    }

    VkQueryPool pool = ctx().timestampPools[_slot];
    if(!pool || !vkIsRecording()) {
        return;
    }

    vkCmdWriteTimestamp2(vkCommandBuffer(), VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, pool, _end);
}

void TimestampQuery::captureFromPool(u32 slot, const u64* ticks, u32 count) {
    if(_state != State::Ended || _slot != slot || _begin == ~0u) {
        return;
    }
    if(_begin >= count || _end >= count) {
        return;
    }

    _time = ticksToSeconds(ticks[_begin], ticks[_end]);
    _state = State::Resolved;
}

Result<double> TimestampQuery::seconds(bool wait) const {
    if(_state == State::Resolved) {
        return {true, _time};
    }

    DEBUG_ASSERT(_state == State::Ended);

    if(_begin == ~0u) {
        _time = 0.0;
        _state = State::Resolved;
        return {true, _time};
    }

    VkQueryPool pool = ctx().timestampPools[_slot];
    if(!pool) {
        _time = 0.0;
        _state = State::Resolved;
        return {true, _time};
    }

    const VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT | (wait ? VK_QUERY_RESULT_WAIT_BIT : 0);

    u64 endTicks = 0;
    const VkResult endResult = vkGetQueryPoolResults(
        vkDevice(), pool, _end, 1, sizeof(endTicks), &endTicks, sizeof(endTicks), flags
    );
    if(endResult == VK_NOT_READY) {
        return {false, {}};
    }
    vkCheck(endResult);

    u64 beginTicks = 0;
    const VkResult beginResult = vkGetQueryPoolResults(
        vkDevice(), pool, _begin, 1, sizeof(beginTicks), &beginTicks, sizeof(beginTicks), flags
    );
    if(beginResult == VK_NOT_READY) {
        return {false, {}};
    }
    vkCheck(beginResult);

    _time = ticksToSeconds(beginTicks, endTicks);
    _state = State::Resolved;

    return {true, _time};
}

}
