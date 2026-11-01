#include "mod/AutoMineMod.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/service/TargetedBedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/material/Material.h"

#include <string_view>

namespace auto_mine {

namespace {

// ================= 可调参数 =================
// 挖掘半径（以人物脚下为中心的立方体，半径 5 即 11×11×11）
constexpr int kRadius = 5;
// 有效触及距离（从眼睛算起）。超过这个距离服务器会拒绝，所以不再尝试。
constexpr double kMaxReach = 5.2;
// 单个方块挖不动时最多尝试多少 tick（20 tick = 1 秒）
constexpr int kTargetTimeoutTicks = 400;
// 放弃后屏蔽多少 tick
constexpr int kSkipTicks = 80;
// 挖掘面（0=下 1=上 2=北 ...）
constexpr uchar kFace = 1;
// ============================================

bool isMineable(Block const& block) {
    if (block.isAir()) {
        return false;
    }

    auto const& material = block.getMaterial();
    if (material.mLiquid) {
        return false;
    }

    // 硬度 < 0 表示不可破坏（基岩、屏障、命令方块等）
    if (block.getBlockType().getDestroySpeed() < 0.0f) {
        return false;
    }

    static constexpr std::string_view kBlacklist[] = {
        "minecraft:bedrock",
        "minecraft:barrier",
        "minecraft:command_block",
        "minecraft:repeating_command_block",
        "minecraft:chain_command_block",
        "minecraft:structure_block",
        "minecraft:structure_void",
        "minecraft:end_portal",
        "minecraft:end_portal_frame",
        "minecraft:portal",
        "minecraft:water",
        "minecraft:flowing_water",
        "minecraft:lava",
        "minecraft:flowing_lava",
    };

    std::string_view name = block.getTypeName();
    for (auto const& blocked : kBlacklist) {
        if (name == blocked) {
            return false;
        }
    }

    return true;
}

std::int64_t keyOf(BlockPos const& pos) {
    constexpr std::int64_t kMask = 0x1FFFFF;
    return ((static_cast<std::int64_t>(pos.x) & kMask) << 42)
         | ((static_cast<std::int64_t>(pos.y) & kMask) << 21)
         | (static_cast<std::int64_t>(pos.z) & kMask);
}

} // namespace

AutoMineMod& AutoMineMod::getInstance() {
    static AutoMineMod instance;
    return instance;
}

bool AutoMineMod::load() {
    getSelf().getLogger().debug("AutoMine loading...");
    return true;
}

bool AutoMineMod::enable() {
    auto& logger = getSelf().getLogger();
    logger.info("AutoMine enabled (radius=5, max reach=5.2)");

    // 客户端 enable 时游戏已启动完成，可以安全订阅事件
    mTickListener = ll::event::EventBus::getInstance().emplaceListener<
        ll::event::world::ClientLevelTickEvent>([this](ll::event::world::ClientLevelTickEvent&) {
        onClientTick();
    });

    return mTickListener != nullptr;
}

bool AutoMineMod::disable() {
    if (mTickListener) {
        // 停止正在进行的挖掘，避免残留在半破坏状态
        if (mTarget) {
            if (auto client = ll::service::getClientInstance()) {
                if (auto* player = client->getLocalPlayer()) {
                    auto* gameMode = player->mGameMode.get();
                    if (gameMode) {
                        gameMode->stopDestroyBlock(*mTarget);
                    }
                }
            }
            mTarget.reset();
        }
        ll::event::EventBus::getInstance().removeListener(mTickListener);
        mTickListener = nullptr;
    }
    getSelf().getLogger().debug("AutoMine disabled");
    return true;
}

void AutoMineMod::onClientTick() {
    ++mTickCounter;

    // 定期清理过期的屏蔽记录
    if ((mTickCounter % 400) == 0) {
        for (auto it = mSkipUntil.begin(); it != mSkipUntil.end();) {
            if (it->second <= mTickCounter) {
                it = mSkipUntil.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto client = ll::service::getClientInstance();
    if (!client) {
        return;
    }

    LocalPlayer* player = client->getLocalPlayer();
    if (player == nullptr || !player->isAlive()) {
        return;
    }

    // mGameMode 是客户端本地玩家的游戏模式对象，它的 start/continue/stopDestroyBlock
    // 会自动按当前游戏模式（生存/创造）向服务器发送正确的方块操作包。
    GameMode* gameMode = player->mGameMode.get();
    if (gameMode == nullptr) {
        return;
    }

    BlockSource& blockSource = player->getDimension().getBlockSourceFromMainChunkSource();

    Vec3 eyePos = player->getPosition();
    eyePos.y += 1.62f; // 眼睛高度

    // ---- 继续挖掘当前目标 ----
    if (mTarget) {
        BlockPos const target = *mTarget;

        if (blockSource.getBlock(target).isAir()) {
            // 方块已被挖掉
            gameMode->stopDestroyBlock(target);
            mTarget.reset();
            mTargetTicks = 0;
        } else if (++mTargetTicks > kTargetTimeoutTicks) {
            // 挖不动（超时），放弃并暂时屏蔽
            gameMode->stopDestroyBlock(target);
            mSkipUntil[keyOf(target)] = mTickCounter + kSkipTicks;
            mTarget.reset();
            mTargetTicks = 0;
        } else {
            bool destroyed = false;
            gameMode->continueDestroyBlock(target, kFace, eyePos, destroyed);
            if (destroyed) {
                gameMode->stopDestroyBlock(target);
                mTarget.reset();
                mTargetTicks = 0;
            }
            return; // 每 tick 只推进一个方块
        }
    }

    // ---- 扫描并选择下一个目标（最近的可挖方块）----
    Vec3 const& feet = player->getPosition();
    BlockPos    center(static_cast<float>(feet.x), static_cast<float>(feet.y), static_cast<float>(feet.z));

    bool     found    = false;
    BlockPos bestPos;
    double   bestDist = 1e9;

    for (int dx = -kRadius; dx <= kRadius; ++dx) {
        for (int dy = -kRadius; dy <= kRadius; ++dy) {
            for (int dz = -kRadius; dz <= kRadius; ++dz) {
                BlockPos pos(center.x + dx, center.y + dy, center.z + dz);

                if (!isMineable(blockSource.getBlock(pos))) {
                    continue;
                }

                auto skipIt = mSkipUntil.find(keyOf(pos));
                if (skipIt != mSkipUntil.end() && skipIt->second > mTickCounter) {
                    continue;
                }

                double dist = eyePos.distanceTo(pos.center());
                if (dist > kMaxReach) {
                    continue;
                }

                if (dist < bestDist) {
                    bestDist = dist;
                    bestPos  = pos;
                    found    = true;
                }
            }
        }
    }

    if (!found) {
        return;
    }

    mTarget      = bestPos;
    mTargetTicks = 0;

    bool destroyed = false;
    gameMode->startDestroyBlock(bestPos, kFace, destroyed);
    if (destroyed) {
        gameMode->stopDestroyBlock(bestPos);
        mTarget.reset();
    }
}

} // namespace auto_mine

LL_REGISTER_MOD(auto_mine::AutoMineMod, auto_mine::AutoMineMod::getInstance());
