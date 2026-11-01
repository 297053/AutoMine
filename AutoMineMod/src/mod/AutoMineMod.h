#pragma once

#include "ll/api/mod/NativeMod.h"
#include "ll/api/event/ListenerBase.h"

#include "mc/world/level/BlockPos.h"

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace auto_mine {

class AutoMineMod {

public:
    static AutoMineMod& getInstance();

    AutoMineMod() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();

private:
    void onClientTick();

private:
    ll::mod::NativeMod& mSelf;

    ll::event::ListenerPtr mTickListener;

    // 当前正在挖的方块
    std::optional<BlockPos> mTarget;
    int                     mTargetTicks = 0;

    // 超时 / 被服务器拒绝的方块，短时间内不再尝试
    std::unordered_map<std::int64_t, std::uint64_t> mSkipUntil;
    std::uint64_t                                   mTickCounter = 0;
};

} // namespace auto_mine
