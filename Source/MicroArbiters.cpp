#include "MicroArbiters.h"


#include "UnitUtil.h"
#include "InformationManager.h"

using namespace UAlbertaBot;

/* CODE ADDED */
// The whole micro script
MicroArbiters::MicroArbiters() 
{
}

const int stasisCastRange = BWAPI::TechTypes::Stasis_Field.getWeapon().maxRange();

void MicroArbiters::executeMicro(const BWAPI::Unitset& targets, const UnitCluster& cluster)
{
    BWAPI::Unitset units = Intersection(getUnits(), cluster.units);
    if (units.empty())
    {
        return;
    }

    // Clean up stale entries
    for (auto it = _unitStatusMap.begin(); it != _unitStatusMap.end(); ) {
        BWAPI::Unit u = it->first;

        if (!u || !u->exists() || u->getPlayer() == the.enemy()) {
            it = _unitStatusMap.erase(it);
        }
        else {
            ++it;
        }
    }

    for (BWAPI::Unit u : cluster.units) {
        ArbiterStatus status;

        if (BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Stasis_Field) && u->getEnergy() && u->getEnergy() >= 100) {
            status = ArbiterStatus::LookingToStasis;
        }
        else {
            status = ArbiterStatus::LookingToCloak;
        }
        if (_unitStatusMap.find(u) == _unitStatusMap.end()) {
            // initialize new member
            _unitStatusMap.emplace(u, status);
            
        }
        else {
            _unitStatusMap[u] = status;
        }
    }

    _arbiterLeashes.clear();

    if (_supportableClusters.empty()) {
        for (BWAPI::Unit arb : units) {
            _arbiterLeashes.emplace(arb->getID(), Leash{arb->getPosition(), BWAPI::Unitset(), 5 * 32});
        }
    }
    else {

        std::sort(_supportableClusters.begin(), _supportableClusters.end(),
            [this](const UnitCluster& a, const UnitCluster& b) {
                int cA = this->getArbiterNum(a);
                int cB = this->getArbiterNum(b);

                if (cA != cB)
                    return cA < cB;

                int scoreA = 0;
                int scoreB = 0;

                for (BWAPI::Unit u : a.units) { scoreA += this->getAllyCloakValue(u); }
                for (BWAPI::Unit u : b.units) { scoreB += this->getAllyCloakValue(u); }

                return scoreA > scoreB;
            });


        int clusterNum = _supportableClusters.size();
        int clusterIndex = 0;

        std::vector<BWAPI::Unit> freeArbiters;
        for (auto a : cluster.units) {
            if (!a || !a->exists()) continue;
            freeArbiters.push_back(a); 
        }


        int safety = 0;
        while (!freeArbiters.empty() && safety++ < 50) {

            if (_supportableClusters.empty()) break;

            UnitCluster cl = _supportableClusters[clusterIndex];

            BWAPI::Unit closestUnit = nullptr;
            int closestDist = INT_MAX;

            for (BWAPI::Unit arbiter : freeArbiters) {
                int dist = arbiter->getDistance(cl.center);

                if (dist < closestDist) {
                    closestUnit = arbiter;
                    closestDist = dist;
                }
            }

            if (closestUnit && closestUnit->exists()) {
                _arbiterLeashes.emplace(closestUnit->getID(), Leash{cl.center, cl.units, std::max(32, cl.radius - 2 * 32)});

                // Remove the arbiter we just used
                freeArbiters.erase(std::remove(freeArbiters.begin(), freeArbiters.end(), closestUnit), freeArbiters.end());
            }

            clusterIndex++;
            if (clusterIndex >= _supportableClusters.size()) { clusterIndex = 0; }
        }
 

    }

    assignTargets(units, targets);
}


// Copied from MicroRanged
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



    bool scheduledStasisCast = false;

    std::unordered_set<int> alive;
    for (BWAPI::Unit u : arbiterUnits) {
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
                _lastStasisCast = BWAPI::Broodwar->getFrameCount();
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

        auto it = _unitStatusMap.find(arbiter);
        if (it == _unitStatusMap.end()) continue;
        ArbiterStatus status = it->second;

        {
            auto it = _arbiterLeashes.find(arbiter->getID());
            if (it == _arbiterLeashes.end()) continue;
            const Leash& leash = it->second;

            BWAPI::Broodwar->drawCircleMap(leash.center.x, leash.center.y, leash.range, BWAPI::Colors::Purple);

            if (arbiter->getDistance(leash.center) > leash.range && status != ArbiterStatus::LookingToStasis) {
                the.micro.Move(arbiter, leash.center);
                BWAPI::Broodwar->drawLineMap(leash.center, arbiter->getPosition(), BWAPI::Colors::Purple);

                continue;
            }
            else if (leash.units.size() > 0) {

                // Cloak range
                BWAPI::Broodwar->drawCircleMap(arbiter->getPosition().x, arbiter->getPosition().y, 5 * 32,
                    (status == ArbiterStatus::LookingToCloak ? BWAPI::Colors::Blue : BWAPI::Colors::Red));
                if (status == ArbiterStatus::LookingToCloak) {
                    BWAPI::Position cloakingPos = getBestCloakPosition(leash.units, 5 * 32);

                    if (arbiter->getDistance(cloakingPos) >= 24) {
                        the.micro.Move(arbiter, cloakingPos);
                        continue;
                    }

                }
                else if (status == ArbiterStatus::LookingToStasis && the.now() - _lastStasisCast >= 30) {

                    BWAPI::Unitset allPotentialTargets = BWAPI::Broodwar->getUnitsInRadius(arbiter->getPosition(), 12 * 32);

                    BWAPI::Position stasisCastPos = getBestStasisCast(arbiter, allPotentialTargets);
                    if (stasisCastPos.isValid()) {
                        if (arbiter->getDistance(stasisCastPos) <= stasisCastRange && !scheduledStasisCast) {
                            arbiter->useTech(BWAPI::TechTypes::Stasis_Field, stasisCastPos);
                            scheduledStasisCast = true;
                        }
                        else {
                            the.micro.Move(arbiter, stasisCastPos);
                        }
                        continue;
                    }
                }
            }


        }

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


int MicroArbiters::getArbiterNum(const UnitCluster& cluster) {
    int count = 0;

    for (BWAPI::Unit u : cluster.units) {
        count += u->getType() == BWAPI::UnitTypes::Protoss_Arbiter;
    }

    return count;
}


void MicroArbiters::regroup(const BWAPI::Position& regroupPosition, const UnitCluster& cluster) const {
    
    UnitCluster newCl;

    for (BWAPI::Unit arbiter : cluster.units) {
        // If we can't cast stasis, or we're too low, or we're alone, go back to normal logic
        if (!canCastStasis(arbiter) || arbiter->getShields() + arbiter->getHitPoints() <= 150 || !hasGroundSupport(arbiter)) {
            newCl.add(arbiter);
        }
        // If none of those conditions apply, that means we can continue and cast
    }

    // Only those that are not in a state to cast
    MicroManager::regroup(regroupPosition, newCl);   // fall back to normal logic
}


void MicroArbiters::findBestSupportClusters(const std::vector<UnitCluster> clusters) {

    // Clear from previous iterration
    _supportableClusters.clear();

    Base* enemyBase = the.bases.enemyStart();
    BWAPI::Position destination =
        enemyBase ? enemyBase->getPosition()
        : the.bases.myMain()->getPosition();



    UnitCluster biggestCluster;

    std::vector<UnitCluster> candidates;

    for (UnitCluster cl : clusters) {

        int dist = cl.air ? cl.center.getApproxDistance(destination) : the.map.getGroundDistance(cl.center, destination);

        if (cl.count > biggestCluster.count) {
            biggestCluster = cl;
        }

        int score = 0;
        for (BWAPI::Unit u : cl.units) {
            score += getAllyCloakValue(u);
        }
        if (score >= 40) {
            candidates.push_back(cl);
        }

    }

    if (candidates.empty()) {
        if (biggestCluster.count > 0) {
            _supportableClusters.push_back(biggestCluster);
        }
    }
    else {
        _supportableClusters = candidates;
    }

}


BWAPI::Position MicroArbiters::getBestCloakPosition(const BWAPI::Unitset & allies, int radius) {
    if (allies.empty()) return BWAPI::Positions::None;

    int bestScore = std::numeric_limits<int>::min();
    BWAPI::Position bestPos = BWAPI::Positions::Invalid;

    // Candidate points: we use ally positions as seeds
    for (const auto& centerUnit : allies) {
        if (!centerUnit || !centerUnit->exists() || !centerUnit->getPosition().isValid())
            continue;

        BWAPI::Position center = centerUnit->getPosition();

        int score = 0;

        for (const auto& unit : allies) {
            if (!unit || !unit->exists()) continue;

            BWAPI::Position pos = unit->getPosition();

            if (center.getDistance(pos) <= radius) {
                score += getAllyCloakValue(unit);
            }
        }

        if (score > bestScore) {
            bestScore = score;
            bestPos = center;
        }
    }

    return bestPos;
}


BWAPI::Position MicroArbiters::getBestStasisCast(BWAPI::Unit caster, const BWAPI::Unitset& targets) {
    if (targets.empty())
        return BWAPI::Positions::None;

    
    int bestScore = std::numeric_limits<int>::min();
    BWAPI::Position bestPos = BWAPI::Positions::None;

    for (const auto& centerUnit : targets) {
        if (!centerUnit || !centerUnit->exists())
            continue;

        BWAPI::TilePosition tile = centerUnit->getTilePosition();

        int left = (tile.x - 1) * 32;
        int top = (tile.y - 1) * 32;

        int right = left + 32 * 3;
        int bottom = top + 32 * 3;

        int score = 0;

        for (const auto& unit : targets) {
            if (!unit || !unit->exists())
                continue;

            BWAPI::Position pos = unit->getPosition();

            if (pos.x >= left && pos.x < right && pos.y >= top && pos.y < bottom) {
                score += getEnemyFreezeValue(caster, unit);
            }
        }

        if (score > bestScore && score >= 25) {
            bestScore = score;

            // Return center of the 3x3 area
            bestPos = BWAPI::Position(left + 32 * 3 / 2, top + 32 * 3 / 2);
        }
    }

    return bestPos;
}




int MicroArbiters::getAllyCloakValue(BWAPI::Unit ally) {
    
    if (!ally || !ally->exists() || ally->getType().isBuilding()) return 0;

    int score = 0;

    if (ally->getType().isCloakable() || ally->getType().hasPermanentCloak()) {
        score = 0;
    }
    else {
        switch (ally->getType()) {
            case BWAPI::UnitTypes::Protoss_Carrier:
                score = 10;
                break;
            case BWAPI::UnitTypes::Protoss_Dragoon:
                score = 3;
                break;
            case BWAPI::UnitTypes::Protoss_Arbiter:
                score = 0;
                break;
            default:
                if (ally->isFlying())   { score = 3; }
                else                    { score = 1; }
                break;

        }
    }

    return score;
}


int MicroArbiters::getEnemyFreezeValue(BWAPI::Unit caster, BWAPI::Unit target) {
    if (!target || !target->exists()) return 0;
    if (target->getType().isBuilding() || target->isFlying()) return 0;
    if (target->isStasised() || target->isUnderDisruptionWeb() || target->isMaelstrommed()) return 0;

    using namespace BWAPI::UnitTypes;

    int score = 0;
    BWAPI::UnitType type = target->getType();

    // Massive anti-carrier threats
    if (type == Terran_Goliath)              score += 15;
    else if (type == Protoss_Dragoon)        score += 12;
    else if (type == Zerg_Hydralisk)         score += 10;
    else if (type == Protoss_Archon)         score += 10;
    else if (type == Terran_Marine)          score += 5;
    else if (type == Terran_Medic)           score += 4;

    // Tanks are big and clunky - this should at least mess with enemy pathfinding
    else if (type == Terran_Siege_Tank_Siege_Mode) score += 8;
    else if (type == Terran_Siege_Tank_Tank_Mode)  score += 6;

    // Bonus for expensive units
    score += (type.mineralPrice() / 100);
    score += (type.gasPrice() / 75);

    // Bonus for units currently fighting
    if (target->isAttacking())
        score += 3;

    // Bonus for spellcasters
    if (type.maxEnergy() > 0)
        score += 5;

    if (score <= 2) {
        if (UnitUtil::CanAttackAir(target)) {
            score += 3;
        }
        else if (UnitUtil::CanAttackGround(target)) {
            score += 1;
        }
    }

    if (score >= 5 && caster->getDistance(target) <= stasisCastRange) {
        score += 5;
    }

    // Slope bonus to block ramps
    if (isOnRamp(target->getPosition()) || unitNearChokepoint(target))
        score *= 2;
    if (target->getPlayer() == the.enemy()) {
        return score;
    }
    else {
        return -score;
    }
}


// This can return null if no target is worth attacking.
BWAPI::Unit MicroArbiters::getTarget(BWAPI::Unit arbiter, const BWAPI::Unitset& targets, bool underThreat)
{
    int bestScore = INT_MIN;
    BWAPI::Unit bestTarget = nullptr;

    for (BWAPI::Unit target : targets)
    {
        if (!target || !target->exists()) continue;

        int priority = getAttackPriority(arbiter, target);             // 0..12
        const int range = arbiter->getDistance(target);                // 0..map diameter in pixels
        const int closerToGoal =                                       // positive if target is closer than us to the goal
            arbiter->getDistance(order->getPosition()) - target->getDistance(order->getPosition());


        // Skip targets that are too far away to worry about--outside tank range.
        if (range >= 5 * 32)
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


// Copied from MicroRanged
// How much do we want to attack this enemy target?
int MicroArbiters::getAttackPriority(BWAPI::Unit arbiter, BWAPI::Unit target)
{
    const BWAPI::UnitType targetType = target->getType();

    // An addon other than a completed comsat is boring.
    // TODO should also check that it is attached
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