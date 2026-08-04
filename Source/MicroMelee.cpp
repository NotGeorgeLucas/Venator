#include "MicroManager.h"
#include "MicroMelee.h"

#include "Bases.h"
#include "InformationManager.h"
#include "WorkerManager.h"
#include "The.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// NOTE Melee units are ground units only. Scourge is treated as a ranged unit.

MicroMelee::MicroMelee()
{ 
}

void MicroMelee::executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster)
{
    BWAPI::Unitset units = Intersection(getUnits(), cluster.units);
    if (units.empty())
    {
        return;
    }
    assignTargets(units, targets);
}


static bool isCombatMeleeUnit(BWAPI::Unit u) {
    if (!u || !u->exists()) return false;
    if (u->getType().isWorker()) {
        return WorkerManager::Instance().isCombatWorker(u);
    }

    return UnitUtil::CanAttackGround(u);
}

int _lastDidCatchMove = 0;

void MicroMelee::assignTargets(const BWAPI::Unitset & meleeUnits, const BWAPI::Unitset & targets)
{
    BWAPI::Unitset meleeUnitTargets;
    for (BWAPI::Unit target : targets) 
    {
        if (!target->isFlying() &&
            target->getType() != BWAPI::UnitTypes::Zerg_Larva &&
            target->getType() != BWAPI::UnitTypes::Zerg_Egg &&
            !infestable(target) &&
            !target->isUnderDisruptionWeb())             // melee unit can't attack under dweb
        {
            meleeUnitTargets.insert(target);
        }
    }


    /* CODE ADDED */
    // Dark Archon merge control

    int daCount = the.my.completed.count(BWAPI::UnitTypes::Protoss_Dark_Archon);
    BWAPI::Unitset daMergeGroup;
    BWAPI::Unitset daBusy;

    auto isNearEnemy = [&](BWAPI::Unit u, int radius) -> bool {
        for (BWAPI::Unit t : targets) {
            if (!t || !t->exists()) continue;
            if (t->getPlayer() == BWAPI::Broodwar->self()) continue;
            if (u->getDistance(t) <= radius) return true;
        }
        return false;
        };


    // tech gate + hard cap
    bool allowDA = (BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Mind_Control) || BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Maelstrom)) && daCount < 2;

    const BWAPI::Position daGatherPoint = the.bases.myMain()->getPosition() - BWAPI::Position(32, 32);

    if (allowDA) {
        for (BWAPI::Unit u : meleeUnits) {
            if (!u || !u->exists()) continue;

            if (u->getType() == BWAPI::UnitTypes::Protoss_Dark_Templar) {
                const int framesSince = BWAPI::Broodwar->getFrameCount() - u->getLastCommandFrame();
                const bool longEnough = framesSince >= 12;

                if (u->getOrder() == BWAPI::Orders::DarkArchonMeld) {
                    if (framesSince > 5 * 24) {
                        the.micro.Move(u, daGatherPoint);
                    }
                }
                else if (u->getLastCommand().getType() == BWAPI::UnitCommandTypes::Use_Tech_Unit && !longEnough) {
                    // StarCraft latency moment
                }
                else if (u->getOrder() == BWAPI::Orders::PlayerGuard && !isNearEnemy(u, 8 * 32)) {
                    daMergeGroup.insert(u);
                }
                else {
                    if (u->getDistance(daGatherPoint) >= 3 * 32) {
                        the.micro.Move(u, daGatherPoint);
                    }
                    else {
                        the.micro.Stop(u);
                    }
                }
            }
        }

        // pick closest pair like HT code
        int closestDist = MAX_DISTANCE;
        BWAPI::Unit a = nullptr;
        BWAPI::Unit b = nullptr;

        for (BWAPI::Unit u1 : daMergeGroup) {
            for (BWAPI::Unit u2 : daMergeGroup) {
                if (u1 == u2) break;

                int dist = u1->getDistance(u2);
                if (dist < closestDist) {
                    closestDist = dist;
                    a = u1;
                    b = u2;
                }
            }
        }

        if (a && b) {
            daBusy.insert(a);
            daBusy.insert(b);
            (void)the.micro.MergeArchon(a, b);
        }
    }

    /* CODE ADDED */
    int zealotNum = 0;
    for (BWAPI::Unit u : meleeUnits) {
        auto uType = u->getType();
        zealotNum += (uType == BWAPI::UnitTypes::Protoss_Zealot);
    }
    bool isZealotSquad = zealotNum >= meleeUnits.size() / 2;

    int tankCount = 0;
    for (auto t : targets)
    {
        if (t->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
            t->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode)
        {
            tankCount++;
        }
        
    }

    bool enemyMostlyTanks = false;

    if (!targets.empty())
    {
        enemyMostlyTanks = (double)tankCount / (double)targets.size() >= 0.65;
    }
    enemySquadSize = targets.size();
    enemyTankSquadSize = tankCount;

    bool squadSafeAgainstTanks = (enemyMostlyTanks && isZealotSquad && zealotNum > tankCount);

    // Are any enemies in range to shoot at the melee units?
    bool underThreat = false;
    if (order->isCombatOrder())
    {
        underThreat = squadSafeAgainstTanks ? false : anyUnderThreat(meleeUnits);
    }



    auto estimateArrival = [&](BWAPI::Unit unit, BWAPI::Position target) {
        if (!unit) return 999999.0f;

        float dist = (float)unit->getDistance(target);
        float baseSpeed = 2.4f;

        float expected = 0.8f; // your "reality is annoying" factor
        float safetyTax = 1.25f; // uncertainty in pathing / slows

        return (dist / (baseSpeed * expected)) * safetyTax;
        };

    for (BWAPI::Unit meleeUnit : meleeUnits)
    {
        // Try to avoid being hit by an undetected enemy dark templar.
        if (the.micro.fleeDT(meleeUnit))
        {
            continue;
        }

        // Don't touch the merging DTs
        if (meleeUnit->getType() == BWAPI::UnitTypes::Protoss_Dark_Templar && daBusy.find(meleeUnit) != daBusy.end()) {
            continue;
        }

        BWAPI::Position trapPosition = the.enemyScoutFollower.getScoutTrapPosition(32);

        if (_lastTrapFrame != the.now() && trapPosition != BWAPI::Positions::None && meleeUnit->getPosition().getApproxDistance(the.bases.myMain()->getCenter()) <= 17 * 32 && the.enemyScoutFollower.getScout()) {
            
            auto scout = the.enemyScoutFollower.getScout();


            if (scout->getPosition().getApproxDistance(meleeUnit->getPosition()) <= 3 * 32 && meleeUnit->getPosition().getApproxDistance(trapPosition) <= 5 * 32) {
                the.micro.CatchAndAttackUnit(meleeUnit, scout);
                BWAPI::Broodwar->drawTextMap(meleeUnit->getPosition().x, meleeUnit->getPosition().y + 32, "TRAP SPRUNG");
            }
            else {
                if (the.now() - _lastDidCatchMove > 8) {
                    _lastDidCatchMove = the.now();
                    the.micro.MoveNear(meleeUnit, trapPosition);
                    BWAPI::Broodwar->drawTextMap(meleeUnit->getPosition().x, meleeUnit->getPosition().y + 32, "MOVING TO TRAP");
                }
            }
            _lastTrapFrame = the.now();
            continue;
        }

        if (order->isCombatOrder())
        {
            if ( !squadSafeAgainstTanks &&
                meleeUnitShouldRetreat(meleeUnit, targets))
            {
                BWAPI::Unit shieldBattery = InformationManager::Instance().nearestShieldBattery(meleeUnit->getPosition());
                if (false &&
                    shieldBattery &&
                    meleeUnit->getDistance(shieldBattery) < 400 &&
                    shieldBattery->getEnergy() >= 10)
                {
                    useShieldBattery(meleeUnit, shieldBattery);	// TODO not working yet
                }
                else
                {
                    // Clustering overrides the retreat once the melee unit retreats far enough to be outside
                    // attack range. So it rarely goes far. The retreat location rarely matters much.
                    BWAPI::Position fleeTo(the.bases.myMain()->getPosition());
                    the.micro.Move(meleeUnit, fleeTo);
                }
            }
            else
            {
                BWAPI::Unit target = getTarget(meleeUnit, meleeUnitTargets, underThreat);
                if (target)
                {
                    the.micro.CatchAndAttackUnit(meleeUnit, target);
                }
                else if (meleeUnit->getDistance(order->getPosition()) > 96)
                {
                    // There are no targets. Move to the order position if not already close.
                    the.micro.Move(meleeUnit, order->getPosition());
                }
            }
        }

        if (Config::Debug::DrawUnitTargets)
        {
            BWAPI::Broodwar->drawLineMap(meleeUnit->getPosition(), meleeUnit->getTargetPosition(),
                Config::Debug::ColorLineTarget);
        }
    }
}

// Choose a target from the set.
// underThreat is true if any of the melee units is under immediate threat of attack.
BWAPI::Unit MicroMelee::getTarget(BWAPI::Unit meleeUnit, const BWAPI::Unitset & targets, bool underThreat)
{
    int bestScore = INT_MIN;
    BWAPI::Unit bestTarget = nullptr;

    for (const auto target : targets)
    {
        const int priority = getAttackPriority(meleeUnit, target);		// 0..12
        const int range = meleeUnit->getDistance(target);				// 0..map size in pixels
        const int closerToGoal =										// positive if target is closer than us to the goal
            meleeUnit->getDistance(order->getPosition()) - target->getDistance(order->getPosition());

        // Skip targets that are too far away to worry about.
        if (range >= 13 * 32)
        {
            continue;
        }

        // Don't chase targets that we can't catch.
        if (!CanCatchUnit(meleeUnit, target))
        {
        	continue;
        }

        // Let's say that 1 priority step is worth 64 pixels (2 tiles).
        // We care about unit-target range and target-order position distance.
        int score = 2 * 32 * priority - range;

        // Adjust for special features.

        // Prefer targets under dark swarm, on the expectation that then we'll be under it too.
        // It doesn't matter whether the target is a building.
        if (target->isUnderDarkSwarm())
        {
            if (meleeUnit->getType().isWorker())
            {
                // Workers can't hit under dark swarm. Skip this target.
                continue;
            }
            score += 4 * 32;
        }

        if (target->isUnderStorm())
        {
            score -= 6 * 32;
        }

        if (!underThreat)
        {
            // We're not under threat. Prefer to attack stuff outside enemy static defense range.
            if (!the.groundHitsFixed.inRange(target))
            {
                score += 2 * 32;
            }
            // Also prefer to attack stuff that can't shoot back.
            if (!UnitUtil::CanAttackGround(target))
            {
                score += 2 * 32;
            }
        }

        // A bonus for attacking enemies that are "in front".
        // It helps reduce distractions from moving toward the goal, the order position.
        if (closerToGoal > 0)
        {
            score += 2 * 32;
        }

        // This could adjust for relative speed and direction, so that we don't chase what we can't catch.
        if (meleeUnit->isInWeaponRange(target))
        {
            if (meleeUnit->getType() == BWAPI::UnitTypes::Zerg_Ultralisk)
            {
                score += 12 * 32;   // because they're big and awkward
            }
            else
            {
                score += 4 * 32;
            }
        }
        else if (!target->isMoving())
        {
            if (target->isSieged() ||
                target->getOrder() == BWAPI::Orders::Sieging ||
                target->getOrder() == BWAPI::Orders::Unsieging)
            {
                score += 48;
            }
            else
            {
                score += 32;
            }
        }
        else if (target->isBraking())
        {
            score += 16;
        }
        else if (target->getPlayer()->topSpeed(target->getType()) >= meleeUnit->getPlayer()->topSpeed(meleeUnit->getType()))
        {
            score -= 2 * 32;
        }

        // Prefer targets that are already hurt.
        if (target->getType().getRace() == BWAPI::Races::Protoss && target->getShields() == 0)
        {
            score += 32;
        }
        else if (target->getHitPoints() < target->getType().maxHitPoints())
        {
            score += 24;
        }

        if (score > bestScore)
        {
            bestScore = score;
            bestTarget = target;
        }
    }

    return bestTarget;
}

int MicroMelee::getAttackPriority(BWAPI::Unit attacker, BWAPI::Unit target) const
{
    BWAPI::UnitType targetType = target->getType();

    // A ghost which is nuking is the highest priority by a mile.
    if (targetType == BWAPI::UnitTypes::Terran_Ghost &&
        target->getOrder() == BWAPI::Orders::NukePaint ||
        target->getOrder() == BWAPI::Orders::NukeTrack)
    {
        return 15;
    }

    // Exceptions for dark templar.
    if (attacker->getType() == BWAPI::UnitTypes::Protoss_Dark_Templar)
    {
        if (targetType == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine)
        {
            return 10;
        }
        if ((targetType == BWAPI::UnitTypes::Terran_Missile_Turret || targetType == BWAPI::UnitTypes::Terran_Comsat_Station) &&
            (BWAPI::Broodwar->self()->deadUnitCount(BWAPI::UnitTypes::Protoss_Dark_Templar) == 0))
        {
            return 9;
        }
        if (targetType == BWAPI::UnitTypes::Zerg_Spore_Colony)
        {
            return 8;
        }
        if (targetType.isWorker())
        {
            return 8;
        }
    }

    /* CODE ADDED */
    if (attacker->getType() == BWAPI::UnitTypes::Protoss_Zealot) {
        if (targetType == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) {
            return 11;
        }
    }

    /* CODE ADDED */
    // Help nearby corsairs with static defenses and cloaked allies with detectors
    if (targetType.isBuilding() && UnitUtil::CanAttackAir(target)) {
        if (!attacker->getUnitsInRadius(8 * 32, BWAPI::Filter::IsAlly && !BWAPI::Filter::IsNeutral && BWAPI::Filter::GetType == BWAPI::UnitTypes::Protoss_Corsair).empty()) {
            return 10;
        }
    }

    if (targetType.isDetector()) {
        if (!attacker->getUnitsInRadius(8 * 32, BWAPI::Filter::IsAlly && !BWAPI::Filter::IsNeutral && (BWAPI::Filter::HasPermanentCloak || BWAPI::Filter::IsCloakable)).empty()) {
            return 10;
        }
    }

    // Short circuit: Enemy unit which is far enough outside its range is lower priority than a worker.
    int enemyRange = UnitUtil::GetAttackRange(target, attacker);
    if (enemyRange &&
        !targetType.isWorker() &&
        attacker->getDistance(target) > 32 + enemyRange)
    {
        return 8;
    }
    // Short circuit: Units before bunkers!
    if (targetType == BWAPI::UnitTypes::Terran_Bunker)
    {
        return 10;
    }
    // Medics and ordinary combat units. Include workers that are doing stuff.
    if (targetType == BWAPI::UnitTypes::Terran_Medic ||
        targetType == BWAPI::UnitTypes::Protoss_High_Templar ||
        targetType == BWAPI::UnitTypes::Zerg_Defiler ||
        UnitUtil::CanAttackGround(target) && !targetType.isWorker())  // includes cannons and sunkens
    {
        return 12;
    }
    if (targetType.isWorker() && (target->isRepairing() || target->isConstructing() || unitNearChokepoint(target)))
    {
        /* CODE ADDED */
        // Focus on workers repairing other workers, because that's a very high priority and happens only in worker base rushes 
        if (target->isRepairing() && target->getTarget() && target->getTarget()->getType() == BWAPI::UnitTypes::Terran_SCV) {
            return 14;
        }

        return 12;
    }
    // next priority is bored workers and turrets
    if (targetType.isWorker() || targetType == BWAPI::UnitTypes::Terran_Missile_Turret)
    {
        return 9;
    }

    return getBackstopAttackPriority(target);
}

// Retreat hurt units to allow them to regenerate health (zerg) or shields (protoss).
bool MicroMelee::meleeUnitShouldRetreat(BWAPI::Unit meleeUnit, const BWAPI::Unitset & targets)
{
    // Terran don't regen so it doesn't make sense to retreat.
    // NOTE We might want to retreat a firebat if medics are available.
    if (meleeUnit->getType().getRace() == BWAPI::Races::Terran)
    {
        return false;
    }

    // we don't want to retreat the melee unit if its shields or hit points are above the threshold set in the config file
    // set those values to zero if you never want the unit to retreat from combat individually
    if (meleeUnit->getShields() > Config::Micro::RetreatMeleeUnitShields || meleeUnit->getHitPoints() > Config::Micro::RetreatMeleeUnitHP)
    {
        return false;
    }

    // if there is a ranged enemy unit within attack range of this melee unit then we shouldn't bother retreating since it could fire and kill it anyway
    for (BWAPI::Unit unit : targets)
    {
        int groundWeaponRange = UnitUtil::GetAttackRange(unit, meleeUnit);
        if (groundWeaponRange >= 64 && unit->getDistance(meleeUnit) < groundWeaponRange)
        {
            return false;
        }
    }

    // A broodling should not retreat since it is on a timer.
    if (meleeUnit->getType() == BWAPI::UnitTypes::Zerg_Broodling)
    {
        return false;
    }

    return true;
}
