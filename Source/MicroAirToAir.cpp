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

const int dwebCastRange = BWAPI::TechTypes::Disruption_Web.getWeapon().maxRange();

std::vector<BWAPI::Unit> corsairCreationOrder;
std::map<BWAPI::Unit, int> corsairOrderIndex;

BWAPI::Unit hunterA = nullptr;
BWAPI::Unit hunterB = nullptr;

std::map<BWAPI::Unit, bool> hasVisitedBase;
std::map<BWAPI::Unit, BWAPI::Position> currentUnitTargetPoint;

std::map<int, int> lastSeenEnergy;
std::map<int, int> energyByUnit;

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

    bool scheduledDWebCast = false;

    std::unordered_set<int> alive;
    for (BWAPI::Unit u : airUnits) {
        if (!u || !u->exists()) continue;
        if (u->getPlayer() != the.self()) continue;
        if (u->getType().maxEnergy() <= 0) continue;

        int id = u->getID();
        int energy = u->getEnergy();

        alive.insert(id);

        auto it = lastSeenEnergy.find(id);
        if (it != lastSeenEnergy.end()) {
            if (energy < it->second) {
                // In theory, dark archons could trigger this, but it's only a second delay on dweb, it's not gonna matter... Right?
                _lastDWebFrame = the.now();
            }
        }

        lastSeenEnergy[id] = energy;
        energyByUnit[id] = energy;
    }

    // purge stale entries (units no longer in cluster / dead / gone / whatever)
    for (auto it = energyByUnit.begin(); it != energyByUnit.end(); ) {
        if (alive.find(it->first) == alive.end()) {
            lastSeenEnergy.erase(it->first);
            it = energyByUnit.erase(it);
        }
        else {
            ++it;
        }
    }


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

    if (the.enemyRace() == BWAPI::Races::Zerg) {

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

    auto hasStructureNearby = [&](const BWAPI::Position& pos, BWAPI::UnitType building) -> bool {
        if (!pos.isValid()) return true;

        const auto& enemies = InformationManager::Instance().getUnitInfo(the.enemy());

        for (const auto& kv : enemies) {
            const UnitInfo& ui = kv.second;

            if (ui.type != building)
                continue;

            if (ui.goneFromLastPosition || ui.lifted)
                continue;

            BWAPI::Position tile = ui.lastPosition;

            if (ui.lastPosition.getDistance(pos) <= 8 * 32)
                return true;
        }

        return false;
        };

    for (BWAPI::Unit airUnit : airUnits)
    {

        if (!hasVisitedBase[airUnit]) {
            if (airUnit->getDistance(enemyBase) < 12 * 32) {
                hasVisitedBase[airUnit] = true;
            }
        }

        bool isHunter = the.enemyRace() == BWAPI::Races::Zerg && (airUnit == hunterA || airUnit == hunterB);


        /* CODE ADDED */
        // Added the whole threat avoidance and overlord hunting logic, oh and also the DWeb logic
        const std::map<BWAPI::Unit, UnitInfo>& enemyForce = InformationManager::Instance().getUnitInfo(the.enemy());
        const std::vector<Threat> threatVector = computeThreats(airUnit, enemyForce);

        if (order->isCombatOrder() || isHunter || !threatVector.empty()) {
            BWAPI::Unit target = getTarget(airUnit, airTargets);

            BWAPI::Broodwar->drawCircleMap(airUnit->getPosition(), dwebCastRange, canDWeb(airUnit) ? BWAPI::Colors::Red : BWAPI::Colors::Blue);

            bool escortCorsair = airUnit->getType() == BWAPI::UnitTypes::Protoss_Corsair && the.enemyRace() != BWAPI::Races::Zerg && (!target || threatVector.empty());


            // Selfish web for overlord hunting
            bool shouldDWeb = false;

            // Support dweb to help allies fight
            bool shouldSupportDWeb = false;
            bool canCast = canDWeb(airUnit);
            bool groundSupport = hasGroundSupport(airUnit);

            // DWeb if we can disable all threats
            BWAPI::Position dwebPos = netForAllThreats(threatVector);
            if (dwebPos.isValid() && canCast && (target || !threatVector.empty()) && the.now() - _lastDWebFrame >= 48) {
                // This is a selfish web, not much use from it if we only cover a single unit when we have support nearby
                if (!groundSupport || !threatVector.empty()) {
                    shouldDWeb = true;
                }
            }

            // Or if we can support
            BWAPI::Position supDWebPos = BWAPI::Positions::Invalid;
            if (!shouldDWeb && canCast && groundSupport && the.now() - _lastDWebFrame >= 48) {


                std::vector<UnitInfo> enemies;
                the.info.getNearbyForce(enemies, airUnit->getPosition(), the.enemy(), 12 * 32);

                supDWebPos = dwebPos.isValid() ? dwebPos : getBestWebCast(airUnit, enemies);
                if (supDWebPos.isValid()) {
                    shouldSupportDWeb = true;
                }
            }


            if (escortCorsair) {
                const std::vector<UnitCluster> allyGroups = the.ops.getFriendlyClusters();

                const UnitCluster* strongestGroup = nullptr;
                int strongestSize = -1;

                std::vector<const UnitCluster*> weakerGroups;

                for (const UnitCluster& group : allyGroups) {
                    int groupSize = 0;
                    for (BWAPI::Unit u : group.units) {
                        if (u->getType() == BWAPI::UnitTypes::Protoss_Carrier) {
                            groupSize += 5;
                        }
                        else if (u->getType() != BWAPI::UnitTypes::Protoss_Corsair) {
                            groupSize++;
                        }
                    }
                    if (groupSize <= 0) continue;

                    if (groupSize > strongestSize) {
                        strongestSize = groupSize;
                        strongestGroup = &group;
                    }
                }

                for (const UnitCluster& group : allyGroups) {
                    if (&group == strongestGroup) continue;
                    if (group.units.empty()) continue;
                    weakerGroups.push_back(&group);
                }

                // Deterministic order for weaker groups so the assignment stays stable.
                std::sort(weakerGroups.begin(), weakerGroups.end(),
                    [](const UnitCluster* a, const UnitCluster* b) {
                        if (a->units.size() != b->units.size())
                            return a->units.size() > b->units.size();
                        if (a->center.x != b->center.x)
                            return a->center.x < b->center.x;
                        return a->center.y < b->center.y;
                    });

                const int corsairSlot = (corsairOrderIndex.find(airUnit) != corsairOrderIndex.end()) ? corsairOrderIndex[airUnit] : 0;

                const UnitCluster* chosenGroup = nullptr;

                if (strongestGroup) {
                    if (weakerGroups.empty()) {
                        chosenGroup = strongestGroup;
                    }
                    else if (corsairSlot < 2) {
                        chosenGroup = strongestGroup;
                    }
                    else {
                        chosenGroup = weakerGroups[(corsairSlot - 2) % weakerGroups.size()];
                    }
                }

                if (chosenGroup) {
                    BWAPI::Position moveTarget = chosenGroup->center;
                    int moveDist = airUnit->getDistance(moveTarget);
                    if ((!canDWeb(airUnit) && moveDist >= 48) || (canDWeb(airUnit) && moveDist >= 4 * 32)) {
                        if (!shouldDWeb && !shouldSupportDWeb) {
                            the.micro.Move(airUnit, moveTarget);
                            BWAPI::Broodwar->drawTextMap(airUnit->getPosition() + BWAPI::Position(0, 32), "Move to escort");
                            continue;
                        }
                    }
                }
                else {
                    if (the.bases.myMain() && the.bases.myMain()->getPosition().isValid()) {
                        // Retreat to main as final fallback
                        the.micro.MoveNear(airUnit, the.bases.myMain()->getPosition());
                        BWAPI::Broodwar->drawTextMap(airUnit->getPosition() + BWAPI::Position(0, 32), "Fall back to base");
                        continue;
                    }
                }
            }

            if (target || !threatVector.empty() || shouldDWeb || shouldSupportDWeb) {
                // A target was found.
                if (Config::Debug::DrawUnitTargets)
                {
                    BWAPI::Broodwar->drawLineMap(airUnit->getPosition(), airUnit->getTargetPosition(), BWAPI::Colors::Purple);
                }


                bool inDanger = false;
                BWAPI::Position fleeVector(0, 0);

                for (auto& threat : threatVector) {
                    int dist;
                    int distA = airUnit->getDistance(threat.currPos);
                    int distB = airUnit->getDistance(threat.predictedPos);
                    dist = std::min(distA, distB);

                    int range = threat.range;

                    if (dist <= range) {
                        inDanger = true;

                        BWAPI::Position away;
                        away = distA < distB? airUnit->getPosition() - threat.currPos : airUnit->getPosition() - threat.predictedPos;
                        fleeVector += away;
                    }
                }

                // We want to attack targets if we can get to them without getting in range of a threat
                bool targetSafe = true;
                BWAPI::Position safeAttackPoint = BWAPI::Positions::Invalid;
                if (target) {

                    for (auto& threat : threatVector) {
                        int threatRange = threat.range;
                        int distThreatToTarget = threat.predictedPos.getDistance(target->getPosition());

                        if (distThreatToTarget <= threatRange) {


                            int attackRange = ((int)((double)UnitUtil::GetAttackRange(airUnit, target)) * 0.8);

                            std::vector<BWAPI::Position> firingPositions;
                            std::vector<BWAPI::Position> safeFiringPositions;

                            for (int angle = 0; angle < 360; angle += 20) {
                                double rad = angle * M_PI / 180.0;

                                int x = target->getPosition().x + int(cos(rad) * (attackRange - 8));
                                int y = target->getPosition().y + int(sin(rad) * (attackRange - 8));

                                BWAPI::Position p(x, y);

                                if (p.isValid())
                                    firingPositions.push_back(p);
                            }

                            for (BWAPI::Position p : firingPositions) {
                                bool isSafe = true;
                                for (auto& threat : threatVector) {
                                    int range = threat.range;

                                    if (p.getDistance(threat.currPos) <= range || p.getDistance(threat.predictedPos) <= range) {
                                        isSafe = false;
                                        break;
                                    }
                                }

                                if (isSafe) {
                                    safeFiringPositions.push_back(p);
                                }
                            }

                            if (safeFiringPositions.size() == 0) {
                                targetSafe = false;
                                break;
                            }
                            else {
                                BWAPI::Position bestPoint = BWAPI::Positions::Invalid;
                                double bestDist = DBL_MAX;

                                for (auto& p : safeFiringPositions) {


                                    if (!pathSafe(airUnit->getPosition(), p, threatVector, airUnit))
                                        continue;

                                    double dist = airUnit->getPosition().getApproxDistance(p);

                                    if (dist < bestDist) {
                                        bestDist = dist;
                                        bestPoint = p;
                                    }
                                }

                                safeAttackPoint = bestPoint;
                            }

                        }
                    }
                }



                // Flee if we're being hit
                if (inDanger) {

                    if (shouldDWeb) {

                        int dist = dwebPos.getDistance(airUnit->getPosition());

                        if (dist <= dwebCastRange && !scheduledDWebCast) {
                            airUnit->useTech(BWAPI::TechTypes::Disruption_Web, dwebPos);
                            scheduledDWebCast = true;
                        }
                        else {
                            the.micro.Move(airUnit, dwebPos);
                        }

                        BWAPI::Broodwar->drawTextMap(airUnit->getPosition() + BWAPI::Position(0, 32), "Webbing selfishly");
                        BWAPI::Broodwar->drawBoxMap(dwebPos.x - 60, dwebPos.y - 40, dwebPos.x + 60, dwebPos.y + 40, BWAPI::Colors::Blue);

                    }
                    else if (shouldSupportDWeb && !scheduledDWebCast) {

                        int dist = supDWebPos.getDistance(airUnit->getPosition());

                        if (dist <= dwebCastRange) {
                            airUnit->useTech(BWAPI::TechTypes::Disruption_Web, supDWebPos);
                            scheduledDWebCast = true;
                        }
                        else {
                            the.micro.Move(airUnit, supDWebPos);
                        }

                        BWAPI::Broodwar->drawTextMap(airUnit->getPosition() + BWAPI::Position(0, 32), "Webbing support");
                        BWAPI::Broodwar->drawBoxMap(supDWebPos.x - 60, supDWebPos.y - 40, supDWebPos.x + 60, supDWebPos.y + 40, BWAPI::Colors::Blue);

                    }
                    else {
                        if (fleeVector != BWAPI::Position(0, 0)) {
                            BWAPI::Position fleeTo = airUnit->getPosition() + fleeVector;
                            
                            int mapW = BWAPI::Broodwar->mapWidth() * 32; int mapH = BWAPI::Broodwar->mapHeight() * 32;

                            // Clamp to map bounds
                            fleeTo.x = std::max(0, std::min(fleeTo.x, mapW - 1));
                            fleeTo.y = std::max(0, std::min(fleeTo.y, mapH - 1));

                            BWAPI::Broodwar->drawTextMap(airUnit->getPosition() + BWAPI::Position(0, 32), "Running away from threat");
                            the.micro.Move(airUnit, fleeTo);
                        }
                    }

                    continue;
                }


                // Reposition instead of full retreat if we need to. Or dweb.
                if (!targetSafe) {

                    if (shouldDWeb) {

                        int dist = dwebPos.getDistance(airUnit->getPosition());

                        if (dist <= dwebCastRange && !scheduledDWebCast) {
                            airUnit->useTech(BWAPI::TechTypes::Disruption_Web, dwebPos);
                            scheduledDWebCast = true;
                        }
                        else {
                            the.micro.Move(airUnit, dwebPos);
                        }

                        BWAPI::Broodwar->drawBoxMap(dwebPos.x - 60, dwebPos.y - 40, dwebPos.x + 60, dwebPos.y + 40, BWAPI::Colors::Blue);
                        BWAPI::Broodwar->drawTextMap(airUnit->getPosition() + BWAPI::Position(0, 32), "Webbing selfishly");
                    }
                    else if (shouldSupportDWeb) {

                        int dist = supDWebPos.getDistance(airUnit->getPosition());

                        if (dist <= dwebCastRange && !scheduledDWebCast) {
                            airUnit->useTech(BWAPI::TechTypes::Disruption_Web, supDWebPos);
                            scheduledDWebCast = true;
                        }
                        else {
                            the.micro.Move(airUnit, supDWebPos);
                        }

                        BWAPI::Broodwar->drawBoxMap(supDWebPos.x - 60, supDWebPos.y - 40, supDWebPos.x + 60, supDWebPos.y + 40, BWAPI::Colors::Blue);
                        BWAPI::Broodwar->drawTextMap(airUnit->getPosition() + BWAPI::Position(0, 32), "Webbing support");
                    }
                    else {

                        int closestDist = INT_MAX;
                        BWAPI::Position closestThreat = BWAPI::Positions::Invalid;
                        for (Threat t : threatVector) {
                            int dist = t.currPos.getDistance(airUnit->getPosition());
                            if (dist < closestDist) {
                                closestThreat = t.currPos;
                                closestDist = dist;
                            }
                        }
                        BWAPI::Position kitePos = the.bases.myStart()->getPosition();
                        if (closestThreat.isValid()) {
                            BWAPI::Position dir = airUnit->getPosition() - closestThreat;
                            kitePos = airUnit->getPosition() + dir * 2;
                        }

                        the.micro.Move(airUnit, kitePos);
                        BWAPI::Broodwar->drawTextMap(airUnit->getPosition() + BWAPI::Position(0, 32), "Move into attack position");
                    }

                    continue;
                }
                if (target) {

                    int attackRange = UnitUtil::GetAttackRange(airUnit, target);

                    bool atSafeAttackPoint = safeAttackPoint.isValid() && airUnit->getPosition().getApproxDistance(safeAttackPoint) < 24;

                    bool canHitTarget = airUnit->getDistance(target) <= attackRange;

                    if (atSafeAttackPoint && canHitTarget) {
                        the.micro.AttackUnit(airUnit, target);
                        BWAPI::Broodwar->drawTextMap(airUnit->getPosition() + BWAPI::Position(0, 32), "Attack safely");
                        continue;
                    }

                    BWAPI::Position start = airUnit->getPosition();
                    BWAPI::Position end = target->getPosition();
                    if (safeAttackPoint.isValid()) { end = safeAttackPoint; }

                    bool safeToMove = true;

                    for (auto& threat : threatVector) {
                        BWAPI::Position tPos = threat.currPos;
                        BWAPI::Position tPosPred = threat.predictedPos;
                        auto threatAttackRange = threat.range;
                        if (lineIntersectsCircle(start.x, start.y, end.x, end.y, tPos.x, tPos.y, threatAttackRange)) {
                            safeToMove = false;
                            break;
                        }
                        if (lineIntersectsCircle(start.x, start.y, end.x, end.y, tPosPred.x, tPosPred.y, threatAttackRange)) {
                            safeToMove = false;
                            break;
                        }
                    }
                    // Go kill things if we're safe
                    if (safeToMove) {
                        the.micro.CatchAndAttackUnit(airUnit, target);
                        BWAPI::Broodwar->drawTextMap(airUnit->getPosition() + BWAPI::Position(0, 32), "Attack safely");
                    }
                    else {
                        BWAPI::Position start = airUnit->getPosition();
                        BWAPI::Position end = target->getPosition();
                        if (safeAttackPoint.isValid()) { end = safeAttackPoint; }

                        BWAPI::Position dir = end - start;
                        BWAPI::Position midpoint = start + dir / 2;

                        if (dir == BWAPI::Position(0, 0)) {
                            the.micro.AttackUnit(airUnit, target);
                            BWAPI::Broodwar->drawTextMap(airUnit->getPosition() + BWAPI::Position(0, 32), "Attack safely");
                            continue;
                        }

                        double midpointDist = airUnit->getPosition().getApproxDistance(midpoint);

                        bool moved = false;

                        BWAPI::Position bestMove;
                        double bestScore = INT_MIN;

                        for (float displace = 16; displace <= midpointDist; displace += 16) {
                            BWAPI::Position midpointA = midpoint + radialOffset(dir, displace);
                            BWAPI::Position midpointB = midpoint + radialOffset(dir, -displace);

                            auto evaluateCandidate = [&](const BWAPI::Position& p) -> bool {
                                for (auto& threat : threatVector) {
                                    if (!p.isValid()) return false;
                                    BWAPI::Position tPos = threat.currPos;
                                    BWAPI::Position tPosPred = threat.predictedPos;
                                    int range = threat.range;

                                    if (lineIntersectsCircle(start.x, start.y, p.x, p.y, tPos.x, tPos.y, range)) {
                                        return false; // unsafe
                                    }
                                    if (lineIntersectsCircle(start.x, start.y, end.x, end.y, tPosPred.x, tPosPred.y, range)) {
                                        break;
                                    }
                                }

                                // simple preference: closer to target = better
                                double score = -start.getDistance(p);

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
                            BWAPI::Broodwar->drawTextMap(airUnit->getPosition() + BWAPI::Position(0, 32), "Dodge threat");
                            moved = true;
                        }

                        if (!moved) {

                            if (isHunter && hasVisitedBase[airUnit]) {
                                handleHunterPatrol(airUnit, enemyBase, enemyNatural);
                                BWAPI::Broodwar->drawTextMap(airUnit->getPosition() + BWAPI::Position(0, 32), "Patrol mode");
                                continue;
                            }

                            BWAPI::Position escape = start + (start - end);
                            the.micro.Move(airUnit, escape);
                        }
                    }
                }
            }
            else {
                // No target found. Go to the attack position.


                if (isHunter && hasVisitedBase[airUnit]) {
                    handleHunterPatrol(airUnit, enemyBase, enemyNatural);
                }
                else {

                    BWAPI::Position goal = order ? order->getPosition() : BWAPI::Positions::Invalid;

                    if (!goal.isValid()) {
                        goal = the.bases.enemyStart() ? the.bases.enemyStart()->getPosition() : BWAPI::Position(0, 0);
                    }

                    if (the.enemyRace() == BWAPI::Races::Zerg || (!hasGroundSupport(airUnit) && !threatVector.empty())) {
                        the.micro.AttackMove(airUnit, goal);
                        BWAPI::Broodwar->drawTextMap(airUnit->getPosition() + BWAPI::Position(0, 32), "Attack-move to order");
                        continue;
                    }
                    else {
                        the.micro.HoldPosition(airUnit);
                        BWAPI::Broodwar->drawTextMap(airUnit->getPosition() + BWAPI::Position(0, 32), "Holding position");
                        continue;
                    }

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
        if (range >= 14 * 32)
        {
            continue;
        }

        // Let's say that 1 priority step is worth 160 pixels (5 tiles).
        // We care about unit-target range and target-order position distance.
        int score = 0;
        if (target->getType() == BWAPI::UnitTypes::Zerg_Overlord) {
            score = 5 * 32 * priority - (int)((double)range / 10.0);
        }
        else {
            score = 5 * 32 * priority - range;
        }

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
            // Significant bonuses scaling with lower health
            double hp = target->getHitPoints();
            double maxHp = target->getType().maxHitPoints();

            double missingRatio = 1.0 - (hp / maxHp); // 0 = full HP, 1 = dead

            // Base bonus
            score += 24;

            // Scaling bonus
            score += static_cast<int>(missingRatio * 300);
        }


        /* CODE ADDED */
        // Used to be a to-do, A2A units should prioritize clumps
        BWAPI::Unitset nearbyEnemies = BWAPI::Broodwar->getUnitsInRadius(target->getPosition(), 32,
            BWAPI::Filter::IsEnemy && BWAPI::Filter::IsFlyer);
        for (auto & enemy : nearbyEnemies) {
            if (enemy->getID() != target->getID()) {
                int dist = enemy->getDistance(target);

                if (dist <= 50) {
                    score += 30;
                    continue;
                }
                if (dist <= 100) {
                    score += 15;
                    continue;
                }
            }
        }

        if (score > bestScore)
        {
            bestScore = score;
            bestTarget = target;
        }
    }
    
    return bestTarget;
}



// Threats are enemies that can hit us and we can't hit back
std::vector<MicroAirToAir::Threat> MicroAirToAir::computeThreats(BWAPI::Unit & airUnit, const std::map<BWAPI::Unit, UnitInfo> & enemyForce) const {
    std::vector<Threat> threatVector;
    for (auto& kv : enemyForce) {
        UnitInfo ui = kv.second;

        if (!ui.unit || !ui.unit->exists()) continue;

        BWAPI::Unit u = ui.unit;

        if (!ui.lastPosition.isValid()) continue;
        if (ui.lastPosition.getDistance(airUnit->getPosition()) > 10 * 32)  continue;


        bool isThreat = ((!u->isFlying() && u->getType().airWeapon() != BWAPI::WeaponTypes::None) || (u->getBuildType() && u->getBuildType() == BWAPI::UnitTypes::Zerg_Spore_Colony))
            && !u->isUnderDisruptionWeb() && !u->isStasised() && !u->isMaelstrommed();

        if (!isThreat) continue;

        Threat t(ui, airUnit);

        threatVector.push_back(t);
    }

    return threatVector;
}


void MicroAirToAir::handleHunterPatrol(BWAPI::Unit airUnit, const BWAPI::Position& enemyBase, const BWAPI::Position& enemyNatural) {
    std::vector<BWAPI::Position> patrolPoints;

    

    auto hasStructureNearby = [&](const BWAPI::Position& pos, BWAPI::UnitType building) -> bool {
            if (!pos.isValid()) return true;

            const auto& enemies = InformationManager::Instance().getUnitInfo(the.enemy());

            for (const auto& kv : enemies) {
                const UnitInfo& ui = kv.second;

                if (ui.type != building)
                    continue;

                if (ui.goneFromLastPosition || ui.lifted)
                    continue;

                BWAPI::Position tile = ui.lastPosition;

                if (ui.lastPosition.getDistance(pos) <= 8 * 32)
                    return true;
            }

            return false;
        };

    auto hasZergBaseNearby = [&](const BWAPI::Position& pos) -> bool {
            if (!pos.isValid()) return true;

            const auto& enemies = InformationManager::Instance().getUnitInfo(the.enemy());

            for (const auto& kv : enemies) {
                const UnitInfo& ui = kv.second;

                // We care about resource depots in this case
                if (!ui.type.isResourceDepot())
                    continue;
                
                if (ui.goneFromLastPosition || ui.lifted)
                    continue;

                BWAPI::Position tile = ui.lastPosition;

                if (ui.lastPosition.getDistance(pos) <= 10 * 32) // slightly bigger radius than looking for spores
                    return true;
            }

            return false;
        };


    auto getNearbyThreats= [&](const BWAPI::Position& pos) -> std::vector<Threat> {
        std::vector<Threat> spores;

        if (!pos.isValid()) return spores;

        const auto& enemies = InformationManager::Instance().getUnitInfo(the.enemy());

        for (const auto& kv : enemies) {
            const UnitInfo& ui = kv.second;

            if (ui.type.airWeapon() == BWAPI::WeaponTypes::None || ui.type.isFlyer()) continue;
            if (ui.goneFromLastPosition || ui.lifted) continue;
            if (!ui.unit || !ui.unit->exists()) continue;


            Threat t(ui, airUnit);
            if (ui.lastPosition.getDistance(pos) <= 8 * 32) spores.push_back(t);
        }

        return spores;
        };


    auto canSafelyPatrol = [&](const BWAPI::Position& pos) -> bool {

        if (!pos.isValid()) return false;

        if (!hasZergBaseNearby(pos)) return false;

        auto spores = getNearbyThreats(pos);

        // No spores = safe
        if (spores.empty()) return true;

        // Need DWeb capability
        if (!canDWeb(airUnit)) return false;

        // Must be able to cover ALL spores
        BWAPI::Position webPos = netForAllThreats(spores);

        return webPos.isValid();
        };


    if (canSafelyPatrol(enemyBase)) {
        patrolPoints.push_back(enemyBase);
    }

    if (canSafelyPatrol(enemyNatural)) {
        patrolPoints.push_back(enemyNatural);
    }

    if (patrolPoints.empty()) {
        the.micro.AttackMove(airUnit, order->getPosition());
        return;
    }

    // Initialize if missing
    if (currentUnitTargetPoint.find(airUnit) == currentUnitTargetPoint.end() || !currentUnitTargetPoint[airUnit].isValid()) {
        currentUnitTargetPoint[airUnit] = patrolPoints[0];
    }

    BWAPI::Position currentTarget = currentUnitTargetPoint[airUnit];


    if (hasStructureNearby(enemyBase, BWAPI::UnitTypes::Zerg_Spore_Colony)) {
        currentUnitTargetPoint[airUnit] = patrolPoints[0];
    }

    // Switch if reached
    if (airUnit->getDistance(currentTarget) < 5 * 32) {
        if (patrolPoints.size() == 1) {
            currentUnitTargetPoint[airUnit] = patrolPoints[0];
        }
        else {
            currentUnitTargetPoint[airUnit] = (currentTarget == patrolPoints[0]) ? patrolPoints[1] : patrolPoints[0];
        }
    }

    BWAPI::Position targetPos = currentUnitTargetPoint[airUnit];

    BWAPI::Broodwar->drawCircleMap(targetPos, 5 * 32, BWAPI::Colors::Cyan, false);
    BWAPI::Broodwar->drawLineMap(airUnit->getPosition(), targetPos, BWAPI::Colors::Cyan);

    the.micro.Move(airUnit, targetPos);
}


// Returns invalid position if there's no way to cover all threats in DWeb
BWAPI::Position MicroAirToAir::netForAllThreats(const std::vector<Threat> threats) {
    BWAPI::Position webPos = BWAPI::Positions::Invalid;

    if (threats.size() <= 0) { return webPos; }

    int minX = threats[0].predictedPos.x;
    int minY = threats[0].predictedPos.y;
    int maxX = minX;
    int maxY = minY;

    const int netWidth = 120;
    const int netHeight = 80;

    for (const Threat& t : threats) {

        if (t.type.isBuilding() && !t.isFinished) continue;

        BWAPI::Position p = t.predictedPos;
        
        int halfW = t.type.width() / 2;
        int halfH = t.type.height() / 2;

        int left = p.x - halfW;
        int right = p.x + halfW;
        int top = p.y - halfH;
        int bottom = p.y + halfH;

        if (left < minX) minX = left;
        if (right > maxX) maxX = right;
        if (top < minY) minY = top;
        if (bottom > maxY) maxY = bottom;
    }

    // We can't fit all threats in one spot, so return invalid
    if (!(maxX - minX > netWidth || maxY - minY > netHeight)) {
        // Place the web in the middle of the group
        webPos.x = (minX + maxX) / 2;
        webPos.y = (minY + maxY) / 2;
    }

    return webPos;
    
}



BWAPI::Position MicroAirToAir::getBestWebCast(BWAPI::Unit caster, const std::vector<UnitInfo>& targets) {
    if (targets.empty())
        return BWAPI::Positions::None;


    int bestScore = std::numeric_limits<int>::min();
    BWAPI::Position bestPos = BWAPI::Positions::None;

    for (const auto& centerUnitInfo : targets) {
        const auto centerUnit = centerUnitInfo.unit;
        if (!centerUnit || !centerUnit->exists())
            continue;

        BWAPI::Position pos = centerUnit->getPosition();

        int left = pos.x - 60;
        int top = pos.y - 40;

        int right = left + 120;
        int bottom = top + 80;

        int score = 0;

        for (const auto& ui : targets) {
            const auto unit = ui.unit;
            if (!unit || !unit->exists())
                continue;

            BWAPI::Position pos = unit->getPosition();

            if (pos.x >= left && pos.x < right && pos.y >= top && pos.y < bottom) {
                score += getEnemyWebValue(caster, unit);
            }
        }

        if (score > bestScore && score >= 20) {
            bestScore = score;

            bestPos = centerUnit->getPosition();
        }
    }

    return bestPos;
}


int MicroAirToAir::getEnemyWebValue(BWAPI::Unit caster, BWAPI::Unit target) {
    if (!target || !target->exists()) return 0;
    if (target->isFlying() || !target->canAttack() || target->getType().isWorker()) return 0;
    if (target->isStasised() || target->isUnderDisruptionWeb() || target->isMaelstrommed()) return 0;
    if (target->getType().isBuilding() && !target->isCompleted()) return 0;

    using namespace BWAPI::UnitTypes;

    int score = 0;
    BWAPI::UnitType type = target->isCompleted() ? target->getType() : target->getBuildType();


    // Massive anti-carrier threats and static defense
    if (type == Protoss_Photon_Cannon)      score += 20;
    else if (type == Terran_Missile_Turret) score += 20;
    else if (type == Zerg_Spore_Colony)     score += 20;
    else if (type == Terran_Goliath)        score += 15;
    else if (type == Zerg_Sunken_Colony)    score += 12;
    else if (type == Terran_Bunker)         score += 12;
    else if (type == Zerg_Hydralisk)        score += 10;
    else if (type == Protoss_Archon)        score += 10;
    else if (type == Protoss_Dragoon)       score += 8;
    else if (type == Terran_Marine)         score += 4;

    if (score < 4) {
        // Default - judge by unit cost
        score += (type.mineralPrice() / 100);
        score += (type.gasPrice() / 75);
    }

    // Bonus for units currently fighting
    if (target->isAttacking())
        score += 2;

    
    if (score < 4) {
        if (UnitUtil::CanAttackAir(target)) {
            score += 3;
        }
        else if (UnitUtil::CanAttackGround(target)) {
            score += 1;
        }
    }


    if (score >= 5 && caster->getDistance(target) <= dwebCastRange) {
        score += 5;
    }

    return score;
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



void MicroAirToAir::regroup(const BWAPI::Position& regroupPosition, const UnitCluster& cluster) const {

    UnitCluster newCl;

    for (BWAPI::Unit unit : cluster.units) {
        // If we can't cast stasis, or we're too low, or we're alone, go back to normal logic
        if (!canDWeb(unit) || unit->getShields() + unit->getHitPoints() <= 150 || !hasGroundSupport(unit)) {
            newCl.add(unit);
        }
        // If none of those conditions apply, that means we can continue and cast
    }

    // Only those that are not in a state to cast
    MicroManager::regroup(regroupPosition, newCl);   // fall back to normal logic
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
