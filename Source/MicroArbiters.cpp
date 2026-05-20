#include "MicroArbiters.h"


#include "UnitUtil.h"
#include "InformationManager.h"

using namespace UAlbertaBot;

// -----------------------------------------------------------------------------------------

MicroArbiters::MicroArbiters() 
{
}

void MicroArbiters::executeMicro(const BWAPI::Unitset& targets, const UnitCluster& cluster)
{
    BWAPI::Unitset units = Intersection(getUnits(), cluster.units);
    if (units.empty())
    {
        return;
    }
    assignTargets(units, targets);
}

void MicroArbiters::assignTargets(const BWAPI::Unitset& arbiterUnits, const BWAPI::Unitset& targets)
{
    // The set of potential targets.
    BWAPI::Unitset arbiterTargets;
    std::copy_if(targets.begin(), targets.end(), std::inserter(arbiterTargets, arbiterTargets.end()),
        [=](BWAPI::Unit u) {
            return
                u->getType() != BWAPI::UnitTypes::Zerg_Larva &&
                u->getType() != BWAPI::UnitTypes::Zerg_Egg &&
                !infestable(u);
        });

    // Figure out if the enemy is ready to attack ground or air.
    bool enemyHasAntiGround = false;
    bool enemyHasAntiAir = false;
    for (BWAPI::Unit target : arbiterTargets)
    {
        if (UnitUtil::AttackOrder(target))
        {
            if (UnitUtil::CanAttackGround(target))
            {
                enemyHasAntiGround = true;
            }
            if (UnitUtil::CanAttackAir(target))
            {
                enemyHasAntiAir = true;
            }
        }
    }

    // Are any enemies in range to shoot at us?
    bool underThreat = order->isCombatOrder() && anyUnderThreat(arbiterUnits);

    for (BWAPI::Unit arbiter : arbiterUnits)
    {
        BWAPI::Broodwar->drawCircleMap(arbiter->getPosition().x, arbiter->getPosition().y, 5 * 32, BWAPI::Colors::Blue);

        if (the.micro.fleeDT(arbiter))
        {
            // We fled from an undetected dark templar.
            continue;
        }

        if (order->isCombatOrder())
        {
            BWAPI::Unit target = getTarget(arbiter, arbiterTargets, underThreat);
            if (target)
            {
                if (Config::Debug::DrawUnitTargets)
                {
                    BWAPI::Broodwar->drawLineMap(arbiter->getPosition(), arbiter->getTargetPosition(), BWAPI::Colors::Purple);
                }

                bool kite = arbiter->isFlying() ? enemyHasAntiAir : enemyHasAntiGround;
                if (Config::Micro::KiteWithRangedUnits && kite)
                {
                    the.micro.KiteTarget(arbiter, target);
                }
                else
                {
                    the.micro.CatchAndAttackUnit(arbiter, target);
                }
            }
            else
            {
                // No target found. If we're not near the order position, go there.
                if (arbiter->getDistance(order->getPosition()) > 100)
                {
                    the.micro.MoveNear(arbiter, order->getPosition());
                }
            }
        }
    }
}

// This can return null if no target is worth attacking.
BWAPI::Unit MicroArbiters::getTarget(BWAPI::Unit arbiter, const BWAPI::Unitset& targets, bool underThreat)
{
    int bestScore = INT_MIN;
    BWAPI::Unit bestTarget = nullptr;

    for (BWAPI::Unit target : targets)
    {

        int priority = getAttackPriority(arbiter, target);             // 0..12
        const int range = arbiter->getDistance(target);                // 0..map diameter in pixels
        const int closerToGoal =                                       // positive if target is closer than us to the goal
            arbiter->getDistance(order->getPosition()) - target->getDistance(order->getPosition());

        // Skip targets that are too far away to worry about--outside tank range.
        if (range >= 13 * 32)
        {
            continue;
        }

        // Let's say that 1 priority step is worth 160 pixels (5 tiles).
        int score = 5 * 32 * priority - range;

        // A bonus for attacking enemies that are "in front".
        if (closerToGoal > 0)
        {
            score += 2 * 32;
        }

        if (!underThreat)
        {
            // We're not under threat. Prefer to attack stuff outside enemy static defense range.
            if (arbiter->isFlying() ? !the.airHitsFixed.inRange(target) : !the.groundHitsFixed.inRange(target))
            {
                score += 4 * 32;
            }
        }

        const bool isThreat = UnitUtil::CanAttack(target, arbiter);
        const bool canShootBack = isThreat && range <= 32 + UnitUtil::GetAttackRange(target, arbiter);

        if (isThreat)
        {
            if (canShootBack)
            {
                score += 7 * 32;
            }
            else
            {
                score += 5 * 32;
            }
        }
        else if (!target->isMoving())
        {
            if (target->isSieged() ||
                target->getOrder() == BWAPI::Orders::Sieging ||
                target->getOrder() == BWAPI::Orders::Unsieging ||
                target->isBurrowed())
            {
                score += 48;
            }
            else
            {
                score += 24;
            }
        }
        else if (target->isBraking())
        {
            score += 16;
        }
        else if (target->getPlayer()->topSpeed(target->getType()) >= arbiter->getPlayer()->topSpeed(arbiter->getType()))
        {
            score -= 4 * 32;
        }

        // Prefer targets that are already hurt.
        if (target->getType().getRace() == BWAPI::Races::Protoss && target->getShields() <= 5)
        {
            score += 32;
        }
        if (target->getHitPoints() < target->getType().maxHitPoints())
        {
            score += 24;
        }

        // Take the damage type into account.
        BWAPI::DamageType damage = UnitUtil::GetWeapon(arbiter, target).damageType();
        if (damage == BWAPI::DamageTypes::Explosive)
        {
            if (target->getType().size() == BWAPI::UnitSizeTypes::Large)
            {
                score += 48;
            }
        }
        else if (damage == BWAPI::DamageTypes::Concussive)
        {
            if (target->getType().size() == BWAPI::UnitSizeTypes::Small)
            {
                score += 48;
            }
            else if (target->getType().size() == BWAPI::UnitSizeTypes::Large)
            {
                score -= 48;
            }
        }

        if (score > bestScore)
        {
            bestScore = score;
            bestTarget = target;
        }
    }

    return bestScore > 0 ? bestTarget : nullptr;
}

// How much do we want to attack this enemy target?
int MicroArbiters::getAttackPriority(BWAPI::Unit arbiter, BWAPI::Unit target)
{
    const BWAPI::UnitType targetType = target->getType();

    // An addon other than a completed comsat is boring.
    if (targetType.isAddon() && !(targetType == BWAPI::UnitTypes::Terran_Comsat_Station && target->isCompleted()))
    {
        return 1;
    }

    // A ghost which is nuking is highest priority.
    if (targetType == BWAPI::UnitTypes::Terran_Ghost &&
        target->getOrder() == BWAPI::Orders::NukePaint ||
        target->getOrder() == BWAPI::Orders::NukeTrack)
    {
        return 15;
    }

    // If the target is building something near our base, something is fishy.
    BWAPI::Position ourBasePosition = BWAPI::Position(the.bases.myMain()->getPosition());
    if (target->getDistance(ourBasePosition) < 1000)
    {
        if (target->getType().isWorker() && (target->isConstructing() || target->isRepairing()))
        {
            return 12;
        }
        if (target->getType().isBuilding())
        {
            if (UnitUtil::CanAttackGround(target) || UnitUtil::CanAttackAir(target))
            {
                return 10;
            }
            return 8;
        }
    }

    if (targetType == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine && !target->isBurrowed() ||
        targetType == BWAPI::UnitTypes::Zerg_Infested_Terran)
    {
        return 12;
    }

    if (targetType == BWAPI::UnitTypes::Protoss_High_Templar ||
        targetType == BWAPI::UnitTypes::Zerg_Defiler)
    {
        return 12;
    }

    if (targetType == BWAPI::UnitTypes::Protoss_Reaver ||
        targetType == BWAPI::UnitTypes::Protoss_Arbiter ||
        targetType == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
        targetType == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
    {
        return 11;
    }

    // Short circuit: Give bunkers a lower priority to reduce bunker obsession.
    if (targetType == BWAPI::UnitTypes::Terran_Bunker)
    {
        return 9;
    }

    // Threats can attack us. Exception: Workers are not threats.
    if (UnitUtil::CanAttack(targetType, arbiter->getType()) && !targetType.isWorker())
    {
        if (arbiter->getDistance(target) > 48 + UnitUtil::GetAttackRange(target, arbiter))
        {
            return 8;
        }
        return 10;
    }

    // Droppers are as bad as threats.
    if (targetType == BWAPI::UnitTypes::Terran_Dropship ||
        targetType == BWAPI::UnitTypes::Protoss_Shuttle)
    {
        return 10;
    }

    if (targetType == BWAPI::UnitTypes::Terran_Science_Vessel ||
        targetType == BWAPI::UnitTypes::Zerg_Scourge)
    {
        return 10;
    }

    if (targetType == BWAPI::UnitTypes::Protoss_Observer)
    {
        if (InformationManager::Instance().weHaveCloakTech())
        {
            return 11;
        }
        return 10;
    }

    // Next are workers.
    if (targetType.isWorker())
    {
        if (target->isRepairing() || unitNearChokepoint(target))
        {
            return 11;
        }
        if (target->isConstructing())
        {
            return 10;
        }
        return 9;
    }

    if (targetType == BWAPI::UnitTypes::Protoss_Carrier)
    {
        return 8;
    }

    return getBackstopAttackPriority(target);
}