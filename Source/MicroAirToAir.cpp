#include "MicroAirToAir.h"

#include "OpsBoss.h"
#include "The.h"
#include "Bases.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// The splash air-to-air units: Valkyries, corsairs, devourers.

MicroAirToAir::MicroAirToAir()
{ 
}

void MicroAirToAir::executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster)
{
    BWAPI::Unitset units = Intersection(getUnits(), cluster.units);
    if (units.empty())
    {
        return;
    }
    assignTargets(units, targets);
}


std::vector<BWAPI::Unit> corsairCreationOrder;
std::map<BWAPI::Unit, int> corsairOrderIndex;

BWAPI::Unit hunterA = nullptr;
BWAPI::Unit hunterB = nullptr;

std::map<BWAPI::Unit, bool> hasVisitedBase;
std::map<BWAPI::Unit, BWAPI::Position> currentUnitTargetPoint;

void MicroAirToAir::assignTargets(const BWAPI::Unitset & airUnits, const BWAPI::Unitset & targets)
{
    // The set of potential targets.
    BWAPI::Unitset airTargets;
    std::copy_if(targets.begin(), targets.end(), std::inserter(airTargets, airTargets.end()),
        [=](BWAPI::Unit u) {
        return
            u->isFlying() &&
            !infestable(u);
    });


    for (BWAPI::Unit u : airUnits) {
        if (!u || !u->exists()) continue;

        if (u->getType() == BWAPI::UnitTypes::Protoss_Corsair) {
            if (corsairOrderIndex.find(u) == corsairOrderIndex.end()) {
                corsairOrderIndex[u] = corsairCreationOrder.size();
                corsairCreationOrder.push_back(u);
            }
        }
    }

    BWAPI::Position enemyBase = BWAPI::Positions::Invalid;
    BWAPI::Position enemyNatural = BWAPI::Positions::Invalid;

    auto enemyStart = the.bases.enemyStart();

    if (enemyStart) {
        enemyBase = enemyStart->getPosition();

        auto natural = enemyStart->getNatural();
        if (natural) {
            enemyNatural = natural->getPosition();
        }
    }

    /* CODE ADDED */
    // Make a few corsairs hunt overlords at base


    for (auto it = hasVisitedBase.begin(); it != hasVisitedBase.end(); ) {
        if (!it->first || !it->first->exists()) {
            it = hasVisitedBase.erase(it);
        }
        else {
            ++it;
        }
    }


    BWAPI::Position ref = enemyBase.isValid() ? enemyBase : BWAPI::Position(0, 0);

    // FIX hunter A
    if (!hunterA || !hunterA->exists()) {
        hunterA = pickNewHunter(airUnits, hunterB, nullptr, ref);
    }

    // FIX hunter B
    if (corsairCreationOrder.size() >= 3 && (!hunterB || !hunterB->exists())) {
        hunterB = pickNewHunter(airUnits, hunterA, nullptr, ref);
    }

    // Safety just in case
    if (hunterA && hunterB && hunterA == hunterB) {
        hunterB = pickNewHunter(airUnits, hunterA, nullptr, ref);
    }


    auto isValidUnit = [](BWAPI::Unit u) {
        return u && u->exists() && u->getHitPoints() > 0;
        };

    if (!isValidUnit(hunterA)) hunterA = nullptr;
    if (!isValidUnit(hunterB)) hunterB = nullptr;



    if (enemyBase.isValid()) {
        BWAPI::Broodwar->drawCircleMap(
            enemyBase,
            10 * 32,
            BWAPI::Colors::Yellow,
            false
        );
    }

    for (BWAPI::Unit airUnit : airUnits)
    {

        if (!hasVisitedBase[airUnit]) {
            if (airUnit->getDistance(enemyBase) < 10 * 32) {
                hasVisitedBase[airUnit] = true;
            }
        }

        bool isHunter = (airUnit == hunterA || airUnit == hunterB);
        if (order->isCombatOrder() || isHunter)
        {
            BWAPI::Unit target = getTarget(airUnit, airTargets);
            if (target)
            {
                // A target was found.
                if (Config::Debug::DrawUnitTargets)
                {
                    BWAPI::Broodwar->drawLineMap(airUnit->getPosition(), airUnit->getTargetPosition(), BWAPI::Colors::Purple);
                }


                /* CODE ADDED */
                // Added the whole threat avoidance and overlord hunting logic
                std::vector<UnitInfo> enemyForce;
                InformationManager::Instance().getNearbyForce(enemyForce, airUnit->getPosition(), the.enemy(), 15 * 32);

                // Threats are enemies that can hit us and we can't hit back
                std::vector<BWAPI::Unit> threatVector;
                for (auto& ui : enemyForce) {
                    if (ui.unit && ui.unit->exists()) {
                        auto u = ui.unit;
                        if ((!u->isFlying() && u->getType().airWeapon() != BWAPI::WeaponTypes::None)
                            || u->getType() == BWAPI::UnitTypes::Zerg_Spore_Colony) {   // Spores could be getting built and don't technically have weapons while they are being constructed
                            threatVector.push_back(u);
                        }
                    }
                }


                bool inDanger = false;
                BWAPI::Position fleeVector(0, 0);

                for (auto& threat : threatVector) {
                    int dist = airUnit->getDistance(threat);

                    if (threat->getType() == BWAPI::UnitTypes::Zerg_Scourge) {
                        // bigger radius because they move fast and explode
                        if (dist <= 5 * 32) {
                            inDanger = true;

                            BWAPI::Position away = airUnit->getPosition() - threat->getPosition();
                            fleeVector += away;
                        }
                        continue;
                    }

                    int range = UnitUtil::GetAttackRange(threat, airUnit) + 32;

                    if (dist <= range) {
                        inDanger = true;

                        BWAPI::Position away = airUnit->getPosition() - threat->getPosition();
                        fleeVector += away;
                    }
                }

                // We want to attack targets if we can get to them without getting in range of a threat
                bool targetSafe = true;
                for (auto& threat : threatVector) {
                    int threatRange = UnitUtil::GetAttackRange(threat, airUnit);
                    int distThreatToTarget = threat->getDistance(target);

                    if (distThreatToTarget <= threatRange) {
                        targetSafe = false;
                        break;
                    }
                }


                // Flee if we're being hit
                if (inDanger) {
                    if (fleeVector != BWAPI::Position(0,0)) {
                        BWAPI::Position fleeTo = airUnit->getPosition() + fleeVector;
                        the.micro.Move(airUnit, fleeTo);
                    }
                    continue;
                }


                // Reposition instead of full retreat if we need to
                if (!targetSafe) {
                    BWAPI::Position dir = airUnit->getPosition() - target->getPosition();
                    BWAPI::Position kitePos = airUnit->getPosition() + dir;

                    the.micro.Move(airUnit, kitePos);
                    continue;
                }

                BWAPI::Position start = airUnit->getPosition();
                BWAPI::Position end = target->getPosition();

                bool safeToMove = true;

                for (auto& threat : threatVector) {
                    BWAPI::Position tPos = threat->getPosition();
                    auto threatAttackRange = UnitUtil::GetAttackRange(threat, airUnit);
                    if (lineIntersectsCircle(start.x, start.y, end.x, end.y, tPos.x, tPos.y, threatAttackRange)) {
                        safeToMove = false;
                        break;
                    }
                }
                // Go kill things if we're safe
                if (safeToMove) {
                    the.micro.CatchAndAttackUnit(airUnit, target);
                }
                else {
                    BWAPI::Position start = airUnit->getPosition();
                    BWAPI::Position end = target->getPosition();

                    BWAPI::Position dir = end - start;
                    BWAPI::Position midpoint = start + dir / 2;

                    double midpointDist = airUnit->getPosition().getApproxDistance(midpoint);

                    bool moved = false;

                    BWAPI::Position bestMove;
                    int bestScore = INT_MIN;

                    for (double displace = 16; displace <= midpointDist; displace += 16) {
                        BWAPI::Position midpointA = midpoint + perpendicularOffset(dir, displace);
                        BWAPI::Position midpointB = midpoint + perpendicularOffset(dir, -displace);

                        auto evaluateCandidate = [&](const BWAPI::Position& p) -> bool {
                            for (auto& threat : threatVector) {
                                BWAPI::Position tPos = threat->getPosition();
                                int range = UnitUtil::GetAttackRange(threat, airUnit) + 32;

                                if (lineIntersectsCircle(start.x, start.y, p.x, p.y, tPos.x, tPos.y, range)) {
                                    return false; // unsafe
                                }
                            }

                            // simple preference: closer to target = better
                            int score = -start.getDistance(p);

                            if (score > bestScore) {
                                bestScore = score;
                                bestMove = p;
                            }

                            return true;
                        };

                        evaluateCandidate(midpointA);
                        evaluateCandidate(midpointB);
                    }

                    if (bestScore != INT_MIN) {
                        the.micro.Move(airUnit, bestMove);
                        moved = true;
                    }

                    if (!moved) {

                        if (isHunter && hasVisitedBase[airUnit])
                        {
                            handleHunterPatrol(airUnit, enemyBase, enemyNatural);
                            continue;
                        }

                        BWAPI::Position escape = start + (start - end);
                        the.micro.Move(airUnit, escape);
                    }
                }
            }
            else
            {
                // No target found. Go to the attack position.


                if (isHunter && hasVisitedBase[airUnit])
                {
                    handleHunterPatrol(airUnit, enemyBase, enemyNatural);
                }
                else
                {
                    the.micro.AttackMove(airUnit, order->getPosition());
                }

            }
        }
    }
}

// This could return null if no target is worth attacking, but doesn't happen to.
BWAPI::Unit MicroAirToAir::getTarget(BWAPI::Unit airUnit, const BWAPI::Unitset & targets)
{
    int bestScore = INT_MIN;
    BWAPI::Unit bestTarget = nullptr;

    for (const auto target : targets)
    {
        const int priority = getAttackPriority(airUnit, target);		// 0..12
        const int range = airUnit->getDistance(target);					// 0..map size in pixels
        const int closerToGoal =										// positive if target is closer than us to the goal
            airUnit->getDistance(order->getPosition()) - target->getDistance(order->getPosition());

        // Skip targets that are too far away to worry about.
        if (range >= 13 * 32)
        {
            continue;
        }

        // Let's say that 1 priority step is worth 160 pixels (5 tiles).
        // We care about unit-target range and target-order position distance.
        int score = 5 * 32 * priority - range;

        // Adjust for special features.
        // A bonus for attacking enemies that are "in front".
        // It helps reduce distractions from moving toward the goal, the order position.
        if (closerToGoal > 0)
        {
            score += 3 * 32;
        }

        // This could adjust for relative speed and direction, so that we don't chase what we can't catch.
        if (airUnit->isInWeaponRange(target))
        {
            score += 4 * 32;
        }
        else if (!target->isMoving())
        {
            score += 24;
        }
        else if (target->isBraking())
        {
            score += 16;
        }
        else if (target->getPlayer()->topSpeed(target->getType()) >= airUnit->getPlayer()->topSpeed(airUnit->getType()))
        {
            score -= 5 * 32;
        }
        
        // Prefer targets that are already hurt.
        if (target->getType().getRace() == BWAPI::Races::Protoss && target->getShields() == 0)
        {
            score += 32;
        }
        if (target->getHitPoints() < target->getType().maxHitPoints())
        {
            /* CODE ADDED */
            // Significant bonuses for even lower health enemies
            if (target->getHitPoints() < ((double)target->getType().maxHitPoints()) * 0.65f) {
                score += 48;
            }
            score += 24;
        }


        /* CODE ADDED */
        // Used to be a to-do, A2A units should prioritize clumps
        int nearbyEnemies = BWAPI::Broodwar->getUnitsInRadius(target->getPosition(), 32,
            BWAPI::Filter::IsEnemy && BWAPI::Filter::IsFlyer).size();
        score += nearbyEnemies * 20;

        if (score > bestScore)
        {
            bestScore = score;
            bestTarget = target;
        }
    }
    
    return bestTarget;
}


void MicroAirToAir::handleHunterPatrol(BWAPI::Unit airUnit, const BWAPI::Position& enemyBase, const BWAPI::Position& enemyNatural) {
    std::vector<BWAPI::Position> patrolPoints;


    auto hasSporeNearby = [&](const BWAPI::Position& pos) -> bool
        {
            if (!pos.isValid()) return true; // treat invalid as unsafe

            for (auto& u : BWAPI::Broodwar->enemy()->getUnits())
            {
                if (!u || !u->exists()) continue;

                if (u->getType() != BWAPI::UnitTypes::Zerg_Spore_Colony)
                    continue;

                if (u->getDistance(pos) <= 7 * 32)
                    return true;
            }
            return false;
        };


    if (enemyBase.isValid() && !hasSporeNearby(enemyBase))
        patrolPoints.push_back(enemyBase);

    if (enemyNatural.isValid() && !hasSporeNearby(enemyNatural))
        patrolPoints.push_back(enemyNatural);

    if (patrolPoints.empty()) {
        the.micro.AttackMove(airUnit, order->getPosition());
        return;
    }

    // Initialize if missing
    if (currentUnitTargetPoint.find(airUnit) == currentUnitTargetPoint.end() || !currentUnitTargetPoint[airUnit].isValid()) {
        currentUnitTargetPoint[airUnit] = patrolPoints[0];
    }

    BWAPI::Position currentTarget = currentUnitTargetPoint[airUnit];

    // Switch if reached
    if (airUnit->getDistance(currentTarget) < 4 * 32) {
        if (patrolPoints.size() == 1) {
            currentUnitTargetPoint[airUnit] = patrolPoints[0];
        }
        else {
            currentUnitTargetPoint[airUnit] = (currentTarget == patrolPoints[0]) ? patrolPoints[1] : patrolPoints[0];
        }
    }

    BWAPI::Position targetPos = currentUnitTargetPoint[airUnit];

    BWAPI::Broodwar->drawCircleMap(targetPos, 4 * 32, BWAPI::Colors::Cyan, false);
    BWAPI::Broodwar->drawLineMap(airUnit->getPosition(), targetPos, BWAPI::Colors::Cyan);

    the.micro.Move(airUnit, targetPos);
}


// Hunter replacement logic
BWAPI::Unit MicroAirToAir::pickNewHunter(const BWAPI::Unitset& airUnits, BWAPI::Unit exclude1, BWAPI::Unit exclude2, const BWAPI::Position& reference)
{
    BWAPI::Unit best = nullptr;
    int bestScore = INT_MIN;

    for (BWAPI::Unit u : airUnits) {
        if (!u || !u->exists()) continue;
        if (u->getType() != BWAPI::UnitTypes::Protoss_Corsair) continue;

        if (u == exclude1 || u == exclude2) continue;

        // don’t pick busy fighters
        bool isBusy = u->isAttacking() || u->isStartingAttack();
        int score = 0;

        // distance preference
        score -= u->getDistance(reference);

        // reward idle-ish units
        if (!isBusy) score += 500;

        // mild bonus for healthier units
        score += u->getHitPoints();

        if (score > bestScore) {
            bestScore = score;
            best = u;
        }
    }

    return best;
}



// get the attack priority of a target unit
int MicroAirToAir::getAttackPriority(BWAPI::Unit airUnit, BWAPI::Unit target) 
{
    const BWAPI::UnitType rangedType = airUnit->getType();
    const BWAPI::UnitType targetType = target->getType();

    // Devourers are different from the others.
    if (rangedType == BWAPI::UnitTypes::Zerg_Devourer)
    {
        if (targetType.isBuilding())
        {
            // A lifted building is less important.
            return 1;
        }
        if (targetType == BWAPI::UnitTypes::Zerg_Scourge)
        {
            // Devourers are not good at attacking scourge.
            return 9;
        }

        // Everything else is the same.
        return 10;
    }
    
    // The rest is for valkyries and corsairs.

    // Scourge are dangerous and are the worst.
    if (targetType == BWAPI::UnitTypes::Zerg_Scourge)
    {
        return 10;
    }

    // Threats can attack us back.
    if (UnitUtil::TypeCanAttackAir(targetType))    // includes carriers
    {
        // Enemy unit which is far enough outside its range is lower priority.
        if (airUnit->getDistance(target) > 64 + UnitUtil::GetAttackRange(target, airUnit))
        {
            return 8;
        }
        return 9;
    }
    // Certain other enemies are also bad.
    if (targetType == BWAPI::UnitTypes::Terran_Science_Vessel ||
        targetType == BWAPI::UnitTypes::Terran_Dropship ||
        targetType == BWAPI::UnitTypes::Protoss_Shuttle ||
        targetType == BWAPI::UnitTypes::Protoss_Arbiter ||
        targetType == BWAPI::UnitTypes::Zerg_Overlord)
    {
        return 8;
    }

    // Floating buildings are less important than other units.
    if (targetType.isBuilding())
    {
        return 1;
    }

    // Other air units are a little less important.
    return 7;
}
