#include "TimestampQuery.h"

#include "VkContext.h"

#include <string>
#include <deque>
#include <vector>
#include <array>

#include <volk.h>

namespace OM3D {

namespace profile {
    struct Marker {
        std::string name;
        u32 contained_zones;
        double cpu_time;
        TimestampQuery query;
    };

    static std::vector<Marker> current_frame;
    static std::deque<std::vector<Marker>> queued_frames;
    static std::vector<ProfileZone> ready;

    void destroy_profile() {
        current_frame.clear();
        queued_frames.clear();
        ready.clear();
    }

    u32 begin_profile_zone(const char* name) {
        const u32 index = u32(current_frame.size());

        // Either you forgot to call process_profile_markers every frame, or you have too many marker
        // In the later case, you can just remove this assert
        ALWAYS_ASSERT(index < 65536, "Too many profile markers");

        Marker& marker = current_frame.emplace_back();
        marker.name = name;
        marker.cpu_time = program_time();
        marker.query.begin();

        return index;
    }

    // contained_zones is how many markers nested inside this one, used to draw a tree in the profiler UI.
    void end_profile_zone(u32 zone_id) {
        Marker& marker = current_frame[zone_id];
        marker.cpu_time = program_time() - marker.cpu_time;
        marker.contained_zones = u32(current_frame.size()) - zone_id - 1;
        marker.query.end();
    }
}




// GPU timestamps lag the CPU, so frames sit in a queue until the oldest one's queries are ready.
void process_profile_markers() {
    profile::queued_frames.emplace_back().swap(profile::current_frame);
    DEBUG_ASSERT(profile::current_frame.empty());

    bool any_profile_ready = false;
    std::vector<profile::Marker> ready_frame;
    while(!profile::queued_frames.empty()) {
        auto& frame = profile::queued_frames.front();

        bool ready = true;
        for(auto& marker : frame) {
            if(!marker.query.seconds().is_ok) {
                ready = false;
                break;
            }
        }

        if(ready) {
            any_profile_ready = true;
            ready_frame = std::move(frame);
            profile::queued_frames.pop_front();
        } else {
            break;
        }
    }

    if(any_profile_ready) {
        profile::ready.clear();
        for(auto& marker : ready_frame) {
            ProfileZone& zone = profile::ready.emplace_back();
            zone.name = std::move(marker.name);
            zone.contained_zones = marker.contained_zones;
            zone.cpu_time = float(marker.cpu_time);
            zone.gpu_time = float(marker.query.seconds(true).value);
        }
    }
}

Span<ProfileZone> retrieve_profile() {
    return profile::ready;
}

void reset_timestamp_queries() {
    GraphicsContext& c = ctx();
    const u32 slot = c.frame_index;
    VkQueryPool pool = c.timestamp_pools[slot];
    if(!pool) {
        return;
    }

    const u32 count = c.timestamp_allocated[slot];
    if(count) {
        std::array<u64, timestamp_queries_per_frame> ticks{};
        vk_check(vkGetQueryPoolResults(
            c.device,
            pool,
            0,
            count,
            count * sizeof(u64),
            ticks.data(),
            sizeof(u64),
            VK_QUERY_RESULT_64_BIT
        ));

        for(auto& marker : profile::current_frame) {
            marker.query.capture_from_pool(slot, ticks.data(), count);
        }
        for(auto& frame : profile::queued_frames) {
            for(auto& marker : frame) {
                marker.query.capture_from_pool(slot, ticks.data(), count);
            }
        }
    }

    vkResetQueryPool(c.device, pool, 0, timestamp_queries_per_frame);
    c.timestamp_allocated[slot] = 0;
}

// Some queues report fewer than 64 valid timestamp bits; wrap arithmetic must mask to that width.
static u64 timestamp_mask() {
    const u32 bits = ctx().timestamp_valid_bits;
    if(bits == 0 || bits >= 64) {
        return ~u64(0);
    }
    return (u64(1) << bits) - 1;
}

// Wrap-safe tick delta times timestampPeriod (nanoseconds) to seconds.
static double ticks_to_seconds(u64 begin_ticks, u64 end_ticks) {
    const u64 delta = (end_ticks - begin_ticks) & timestamp_mask();
    return double(delta) * double(ctx().timestamp_period) * 1e-9;
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

TimestampQuery TimestampQuery::create_and_begin() {
    TimestampQuery ts;
    ts.begin();
    return ts;
}

void TimestampQuery::begin() {
    DEBUG_ASSERT(_state == State::None);
    _state = State::Started;

    if(!vk_is_recording()) {
        return;
    }

    GraphicsContext& c = ctx();
    _slot = c.frame_index;
    VkQueryPool pool = c.timestamp_pools[_slot];
    if(!pool) {
        return;
    }

    ALWAYS_ASSERT(c.timestamp_allocated[_slot] + 2 <= timestamp_queries_per_frame, "Too many GPU timestamps this frame");
    _begin = c.timestamp_allocated[_slot];
    _end = _begin + 1;
    c.timestamp_allocated[_slot] += 2;

    vkCmdWriteTimestamp2(vk_command_buffer(), VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, pool, _begin);
}

void TimestampQuery::end() {
    DEBUG_ASSERT(_state == State::Started);
    _state = State::Ended;

    if(_begin == ~0u) {
        return;
    }

    VkQueryPool pool = ctx().timestamp_pools[_slot];
    if(!pool || !vk_is_recording()) {
        return;
    }

    vkCmdWriteTimestamp2(vk_command_buffer(), VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, pool, _end);
}

void TimestampQuery::capture_from_pool(u32 slot, const u64* ticks, u32 count) {
    if(_state != State::Ended || _slot != slot || _begin == ~0u) {
        return;
    }
    if(_begin >= count || _end >= count) {
        return;
    }

    _time = ticks_to_seconds(ticks[_begin], ticks[_end]);
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

    VkQueryPool pool = ctx().timestamp_pools[_slot];
    if(!pool) {
        _time = 0.0;
        _state = State::Resolved;
        return {true, _time};
    }

    const VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT | (wait ? VK_QUERY_RESULT_WAIT_BIT : 0);

    u64 end_ticks = 0;
    const VkResult end_result = vkGetQueryPoolResults(
        vk_device(), pool, _end, 1, sizeof(end_ticks), &end_ticks, sizeof(end_ticks), flags
    );
    if(end_result == VK_NOT_READY) {
        return {false, {}};
    }
    vk_check(end_result);

    u64 begin_ticks = 0;
    const VkResult begin_result = vkGetQueryPoolResults(
        vk_device(), pool, _begin, 1, sizeof(begin_ticks), &begin_ticks, sizeof(begin_ticks), flags
    );
    if(begin_result == VK_NOT_READY) {
        return {false, {}};
    }
    vk_check(begin_result);

    _time = ticks_to_seconds(begin_ticks, end_ticks);
    _state = State::Resolved;

    return {true, _time};
}

}
