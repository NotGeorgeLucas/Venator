#include "MicroRanged.h"

#include "Bases.h"
#include "InformationManager.h"
#include "The.h"
#include "UnitUtil.h"

#include <random>

using namespace UAlbertaBot;

// The unit's ranged ground weapon does splash damage, so it works under dark swarm.
// Firebats are not here: They are melee units.
// Tanks and lurkers are not here: They have their own micro managers.
bool MicroRanged::goodUnderDarkSwarm(BWAPI::UnitType type)
{
    return
        type == BWAPI::UnitTypes::Protoss_Archon ||
        type == BWAPI::UnitTypes::Protoss_Reaver;
}

// -----------------------------------------------------------------------------------------

MicroRanged::MicroRanged()
{
}

void MicroRanged::executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster)
{
    BWAPI::Unitset units = Intersection(getUnits(), cluster.units);
    if (units.empty())
    {
        return;
    }
    assignTargets(units, targets);
}


static std::map<BWAPI::Unit, BWAPI::Unit> carrierToGoliathMap;

static std::mt19937 gen(std::random_device{}());
Base* _currentBaseTarget = nullptr;
void MicroRanged::assignTargets(const BWAPI::Unitset & rangedUnits, const BWAPI::Unitset & targets)
{
    // The set of potential targets.
    BWAPI::Unitset rangedUnitTargets;
    std::copy_if(targets.begin(), targets.end(), std::inserter(rangedUnitTargets, rangedUnitTargets.end()),
        [=](BWAPI::Unit u) {
        return
            u->getType() != BWAPI::UnitTypes::Zerg_Larva &&
            u->getType() != BWAPI::UnitTypes::Zerg_Egg &&
            !infestable(u);
    });

    CarrierThreatInfo cInfo;
    cInfo.maxThreatElevation = INT_MIN;
    cInfo.shouldGoHighground = false;

    // Figure out if the enemy is ready to attack ground or air.
    bool enemyHasAntiGround = false;
    bool enemyHasAntiAir = false;
    for (BWAPI::Unit target : rangedUnitTargets)
    {
        // If the enemy unit is retreating or whatever, it won't attack.
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

        if (target->getType() == BWAPI::UnitTypes::Protoss_Dragoon || target->getType() == BWAPI::UnitTypes::Terran_Goliath) {
            cInfo.threats.insert(target);
            BWAPI::TilePosition tp(target->getPosition());

            int elevation = BWAPI::Broodwar->getGroundHeight(tp);
            if (elevation > cInfo.maxThreatElevation) {
                cInfo.maxThreatElevation = elevation;
            }
        }
    }

    int carrierNum = 0;
    for (BWAPI::Unit u : rangedUnits) {
        carrierNum += (u->getType() == BWAPI::UnitTypes::Protoss_Carrier);
    }

    cInfo.shouldGoHighground = ((int)((float)carrierNum * 1.5) <= cInfo.threats.size());
    
    // Are any enemies in range to shoot at the ranged units?
    bool underThreat = order->isCombatOrder() && anyUnderThreat(rangedUnits);

    for (BWAPI::Unit rangedUnit : rangedUnits)
    {
        if (rangedUnit->isBurrowed())
        {
            // For now, it would burrow only if irradiated. Leave it.
            // Lurkers are controlled elsewhere.
            continue;
        }

        if (the.micro.fleeDT(rangedUnit))
        {
            // We fled from an undetected dark templar.
            continue;
        }

        // Carriers stay at home until they have enough interceptors to be useful,
        // or retreat toward home to rebuild them if they run low.
        // On attack-move so that they're not helpless, but that can cause problems too....
        // Potentially useful for other units.
        // NOTE Regrouping can cause the carriers to move away from home.
        if (stayHomeUntilReady(rangedUnit))
        {
            BWAPI::Position fleeTo(the.bases.myMain()->getPosition());
            the.micro.MoveSafely(rangedUnit, fleeTo);
            continue;
        }


        /* CODE ADDED */
        // Override logic for going straight for enemy base
        if (rangedUnit->getType() == BWAPI::UnitTypes::Protoss_Carrier && !underBaseThreat() && the.my.completed.count(BWAPI::UnitTypes::Protoss_Carrier) >= 4) {
            if (_currentBaseTarget == nullptr) {
                _currentBaseTarget = the.bases.enemyStart();
            }

            BWAPI::Unit target = getTarget(rangedUnit, rangedUnitTargets, underThreat);

            if (target) {
                if (shouldIssueNewOrder(rangedUnit, target)) {
                    
                    doCarrierAttack(rangedUnit, target, cInfo);


                }

                if (target->getType() == BWAPI::UnitTypes::Terran_Goliath)
                {
                    carrierToGoliathMap[rangedUnit] = target;
                }
            }
            else {
                if (_currentBaseTarget) {

                    BWAPI::Position enemyBasePos = _currentBaseTarget->getPosition();
                    int dist = enemyBasePos.getApproxDistance(rangedUnit->getPosition());

                    BWAPI::Broodwar->drawCircle(BWAPI::CoordinateType::Map, enemyBasePos.x, enemyBasePos.y, 32 * 2, BWAPI::Colors::Purple);

                    // If the order is closer than the base, just go there
                    int orderDist = order->getPosition().getDistance(rangedUnit->getPosition());
                    if (orderDist < dist) {
                        the.micro.MoveNear(rangedUnit, order->getPosition());
                        continue;
                    }

                    if (dist > 2 * 32) {
                        the.micro.MoveNear(rangedUnit, enemyBasePos);
                        continue;
                    }
                    else {
                        // We are near enemy main, but no targets. Fall back to global order

                        the.micro.MoveNear(rangedUnit, order->getPosition());
                        continue;

                    }
                }
            }
        }

        if (order->isCombatOrder())
        {
            BWAPI::Unit target = getTarget(rangedUnit, rangedUnitTargets, underThreat);
            if (target)
            {
                if (Config::Debug::DrawUnitTargets)
                {
                    BWAPI::Broodwar->drawLineMap(rangedUnit->getPosition(), rangedUnit->getTargetPosition(), BWAPI::Colors::Purple);
                }

                bool kite = rangedUnit->isFlying() ? enemyHasAntiAir : enemyHasAntiGround;
                /* CODE ADDED */
                // Reworked kiting and default attack for carriers
                if (Config::Micro::KiteWithRangedUnits && kite)
                {
                    if (rangedUnit->getType() != BWAPI::UnitTypes::Protoss_Carrier) {
                        the.micro.KiteTarget(rangedUnit, target);
                    }
                    else {
                        if (shouldIssueNewOrder(rangedUnit, target)) {
                            

                            BWAPI::Position fleeTo = RawDistanceAndDirection(rangedUnit->getPosition(), target->getPosition(), -48);;

                            the.micro.CarrierAttackMove(rangedUnit, target, fleeTo);
                        }
                    }
                    continue;
                }
                else
                {
                    if (rangedUnit->getType() == BWAPI::UnitTypes::Protoss_Carrier) {
                        if (shouldIssueNewOrder(rangedUnit, target)) {
                            
                            doCarrierAttack(rangedUnit, target, cInfo);
                        }
                    }
                    else {
                        the.micro.CatchAndAttackUnit(rangedUnit, target);
                    }
                    continue;
                }
            }
            else
            {
                // No target found. If we're not near the order position, go there.
                if (rangedUnit->getDistance(order->getPosition()) > 100)
                {
                    the.micro.MoveNear(rangedUnit, order->getPosition());
                }
            }
        }
    }
}




/* CODE ADDED */
// Function to know if carriers should back up to help main
// Crude but should work
bool MicroRanged::underBaseThreat() {

    BWAPI::Position basePos = the.bases.myMain()->getPosition();

    Base* natural = the.bases.myNatural();
    if (natural && natural->getOwner() == the.self()) {
        basePos = natural->getPosition();
    }

    std::vector<UnitInfo> enemyForce;
    std::vector<UnitInfo> myForce;

    InformationManager::Instance().getNearbyForce(enemyForce, basePos, the.enemy(), 800);
    InformationManager::Instance().getNearbyForce(myForce, basePos, the.self(), 800);

    int threat = 0;

    for (auto& enemy : enemyForce)
    {
        if (enemy.unit && enemy.unit->exists() && enemy.unit->canAttack()) {
            threat += enemy.estimateHP();
        }
    }


    int defense = 0;

    for (auto& my : myForce)
    {
        if (my.unit && my.unit->exists() && my.unit->canAttack()) {
            defense += my.estimateHP();
        }
     
    }

    bool isBaseDanger = threat > defense * 0.8;

    return isBaseDanger;
}


/* CODE ADDED */
// Vector maths to compute carrier targets
BWAPI::Position MicroRanged::computeCarrierVector(BWAPI::Unit carrier, const std::vector<std::pair<BWAPI::Unit, int>>& targets) {
    BWAPI::Position origin = carrier->getPosition();
    double sumX = 0.0;
    double sumY = 0.0;

    for (const auto& [unit, priority] : targets)
    {
        if (!unit || !unit->exists()) continue;

        BWAPI::Position diff = unit->getPosition() - origin;
        double dist = std::max(32.0, double(origin.getDistance(unit->getPosition())));

        // Normalize
        double dx = diff.x / dist;
        double dy = diff.y / dist;

        // Distance falloff (closer = stronger)
        double weight = priority * (32.0 / dist);

        // Threat detection (very rough)
        bool isThreat = false;

        switch (unit->getType())
        {
            // Terran
            case BWAPI::UnitTypes::Terran_Goliath:
            case BWAPI::UnitTypes::Terran_Wraith:
            case BWAPI::UnitTypes::Terran_Marine:
            case BWAPI::UnitTypes::Terran_Missile_Turret:
            case BWAPI::UnitTypes::Terran_Valkyrie:
                isThreat = true;
                break;

            // Protoss
            case BWAPI::UnitTypes::Protoss_Dragoon:
            case BWAPI::UnitTypes::Protoss_Scout:
            case BWAPI::UnitTypes::Protoss_Photon_Cannon:
            case BWAPI::UnitTypes::Protoss_Corsair:
            case BWAPI::UnitTypes::Protoss_Archon:
                isThreat = true;
                break;

            // Zerg
            case BWAPI::UnitTypes::Zerg_Hydralisk:
            case BWAPI::UnitTypes::Zerg_Mutalisk:
            case BWAPI::UnitTypes::Zerg_Scourge:
            case BWAPI::UnitTypes::Zerg_Devourer:
            case BWAPI::UnitTypes::Zerg_Spore_Colony:
                isThreat = true;
                break;

            default:
                isThreat = false;
                break;
        }

        if (isThreat)
        {
            // Repel instead of attract
            weight *= -0.5;
        }

        sumX += dx * weight;
        sumY += dy * weight;
    }

    BWAPI::Position result(int(sumX * 32 * 6), int(sumY * 32 * 6)); // scale to tiles
    return origin + result;
}


/* CODE ADDED */
// A map to keep track of which goliath each carrier is targetting to split priority better
void MicroRanged::cleanupCarrierTargets()
{
    for (auto it = carrierToGoliathMap.begin(); it != carrierToGoliathMap.end(); )
    {
        BWAPI::Unit carrier = it->first;
        BWAPI::Unit goliath = it->second;

        if (!carrier || !carrier->exists() ||
            !goliath || !goliath->exists())
        {
            it = carrierToGoliathMap.erase(it);
        }
        else
        {
            ++it;
        }
    }
}


/* CODE ADDED */
// Compute carrier vector and do attack-move to that vector
void MicroRanged::doCarrierAttack(BWAPI::Unit carrier, BWAPI::Unit target, CarrierThreatInfo cInfo) {

    BWAPI::Position moveTo;


    bool foundTile = false;

    // Experimental branch turned off
    if (false && cInfo.shouldGoHighground && cInfo.threats.size() > 0) {

        BWAPI::TilePosition cOrigin(carrier->getPosition());

        BWAPI::Unit closestThreat = nullptr;
        int closestDist = INT_MAX;
        
        for (BWAPI::Unit u : cInfo.threats) {
            int dist = u->getPosition().getApproxDistance(carrier->getPosition());

            if (dist < closestDist) {
                closestDist = dist;
                closestThreat = u;
            }
        }


        BWAPI::Position threatDir = averageNormalizedVector(cInfo.threats, BWAPI::Position(cOrigin));
        BWAPI::Position fleeDir = threatDir * -1;


        // Special case: too close/surrounded
        if (closestThreat->getDistance(carrier) <= 48) {
            fleeDir = the.bases.myMain()->getPosition() - carrier->getPosition();

            double len = std::sqrt(fleeDir.x * fleeDir.x + fleeDir.y * fleeDir.y);

            fleeDir /= len;
                
        }


        moveTo = closestThreat->getPosition() + (fleeDir * 9.5 * 32);
        
    }
    else {

        std::vector<std::pair<BWAPI::Unit, int>> nearbyTargets;

        const auto & enemyForceMap = InformationManager::Instance().getUnitInfo(the.enemy());
        BWAPI::Position pos = carrier->getPosition();

        std::vector<UnitInfo> enemyForce;

        for (const auto& kv : enemyForceMap)
        {
            const UnitInfo& ui = kv.second;

            if (!ui.unit || !ui.unit->exists()) continue;
            if (ui.goneFromLastPosition) continue;

            if (ui.isCompleted() && ui.powered)
            {
                enemyForce.push_back(ui);
            }
        }

        for (auto ui : enemyForce) {
            if (!ui.unit || !ui.unit->exists()) continue;

            int priority = getAttackPriority(carrier, ui.unit);

            if (priority > 0) {
                nearbyTargets.emplace_back(ui.unit, priority);
            }
        }

        moveTo = computeCarrierVector(carrier, nearbyTargets);
    }

    the.micro.CarrierAttackMove(carrier, target, moveTo);
}

/* CODE ADDED */
// For carriers: Should we change targets?
bool MicroRanged::shouldIssueNewOrder(BWAPI::Unit unit, BWAPI::Unit target) {

    BWAPI::Unit lastTarget;

    if (target && target->exists()) {
        lastTarget = unit->getLastCommand().getTarget();
    }

    return !lastTarget ||
        !lastTarget->exists() ||
        getAttackPriority(unit, target) > getAttackPriority(unit, lastTarget);
}


// This can return null if no target is worth attacking.
// underThreat is true if any of the melee units is under immediate threat of attack.
BWAPI::Unit MicroRanged::getTarget(BWAPI::Unit rangedUnit, const BWAPI::Unitset & targets, bool underThreat)
{
    int bestScore = INT_MIN;
    BWAPI::Unit bestTarget = nullptr;

    cleanupCarrierTargets();

    for (BWAPI::Unit target : targets)
    {

        /* CODE ADDED */
        // There's no point to attack interceptors as a carrier
        if (rangedUnit->getType() == BWAPI::UnitTypes::Protoss_Carrier && target->getType() == BWAPI::UnitTypes::Protoss_Interceptor) {
            continue;
        }


        /* CODE ADDED */
        // A scout should not concern itself with the recon at our base, just go straight for their base
        if (rangedUnit->getType() == BWAPI::UnitTypes::Protoss_Scout 
            && target->getType().isWorker() 
            && target->getPosition().getApproxDistance(the.bases.myStart()->getPosition()) <= 8 * 32) {
            continue;
        }

        // Skip targets under dark swarm that we can't hit.
        if (target->isUnderDarkSwarm() && !target->getType().isBuilding() && !goodUnderDarkSwarm(rangedUnit->getType()))
        {
            continue;
        }

        int priority = getAttackPriority(rangedUnit, target);		    // 0..12
        const int range = rangedUnit->getDistance(target);				// 0..map diameter in pixels
        const int closerToGoal =										// positive if target is closer than us to the goal
            rangedUnit->getDistance(order->getPosition()) - target->getDistance(order->getPosition());
        
        // Skip targets that are too far away to worry about--outside tank range.
        // CODE ADDED: carriers like to pick off high priority units so it may be benefitial to search further
        if (range >= (13 + (3 * (rangedUnit->getType() == BWAPI::UnitTypes::Protoss_Carrier))) * 32)
        {
            continue;
        }

        // TODO disabled - seems to be wrong, skips targets it should not
        // Don't chase targets that we can't catch.
        //if (!CanCatchUnit(meleeUnit, target))
        //{
        //	continue;
        //}

        // Let's say that 1 priority step is worth 160 pixels (5 tiles).
        // We care about unit-target range and target-order position distance.
        int score;
        if (rangedUnit->getType() == BWAPI::UnitTypes::Protoss_Carrier) {
            if (range <= 8 * 32) {
                score = 5 * 32 * priority;
            }
            else {
                score = 5 * 32 * priority - (range - 8 * 32);

            }
        }
        else {
            score = 5 * 32 * priority - range;
        }

        /* CODE ADDED */
        // Also we don't want to hyperfocus goliaths one at a time so 
        // let's split the damage by lowering priority if that same goliath is already being targetted

        if (rangedUnit->getType() == BWAPI::UnitTypes::Protoss_Carrier && target->getType() == BWAPI::UnitTypes::Terran_Goliath)
        {
            int numTargeting = 0;

            for (const auto& kv : carrierToGoliathMap)
            {
                BWAPI::Unit c = kv.first;
                BWAPI::Unit g = kv.second;
                if (g && g->exists() && g == target && c != rangedUnit)
                {
                    numTargeting++;
                }
            }

            score -= numTargeting * 40;
        }

        if (rangedUnit->getType() == BWAPI::UnitTypes::Protoss_Carrier) {
            score += priority * 40;  // REALLY focus on the prioritized targets
        }

        // Adjust for special features.
        // A bonus for attacking enemies that are "in front".
        // It helps reduce distractions from moving toward the goal, the order position.
        if (closerToGoal > 0)
        {
            score += 2 * 32;
        }

        if (!underThreat)
        {
            // We're not under threat. Prefer to attack stuff outside enemy static defense range.
            if (rangedUnit->isFlying() ? !the.airHitsFixed.inRange(target) : !the.groundHitsFixed.inRange(target))
            {
                score += 4 * 32;
            }
        }

        const bool isThreat = UnitUtil::CanAttack(target, rangedUnit);   // may include workers as threats
        const bool canShootBack = isThreat && range <= 32 + UnitUtil::GetAttackRange(target, rangedUnit);

        if (isThreat)
        {
            if (canShootBack)
            {
                score += 7 * 32;
            }
            else if (rangedUnit->isInWeaponRange(target))
            {
                score += 5 * 32;
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
        else if (target->getPlayer()->topSpeed(target->getType()) >= rangedUnit->getPlayer()->topSpeed(rangedUnit->getType()))
        {
            score -= 4 * 32;
        }
        
        // Prefer targets that are already hurt.
        if (target->getType().getRace() == BWAPI::Races::Protoss && target->getShields() <= 5)
        {
            score += 32;
            if (rangedUnit->getType() == BWAPI::UnitTypes::Protoss_Carrier) { score += 16; }    // Try to focus more on lower HP units as carriers
        }
        if (target->getHitPoints() < target->getType().maxHitPoints())
        {
            score += 24;
            if (rangedUnit->getType() == BWAPI::UnitTypes::Protoss_Carrier) { score += 8; }    // Try to focus more on lower HP units as carriers
        }

        // Prefer to hit air units that have acid spores on them from devourers.
        if (target->getAcidSporeCount() > 0)
        {
            // Especially if we're a mutalisk with a bounce attack.
            if (rangedUnit->getType() == BWAPI::UnitTypes::Zerg_Mutalisk)
            {
                score += 16 * target->getAcidSporeCount();
            }
            else
            {
                score += 8 * target->getAcidSporeCount();
            }
        }

        // Take the damage type into account.
        BWAPI::DamageType damage = UnitUtil::GetWeapon(rangedUnit, target).damageType();
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

// How much do we want to attack this enenmy target?
int MicroRanged::getAttackPriority(BWAPI::Unit rangedUnit, BWAPI::Unit target) 
{
    const BWAPI::UnitType rangedType = rangedUnit->getType();
    const BWAPI::UnitType targetType = target->getType();

    if (rangedType == BWAPI::UnitTypes::Zerg_Guardian && target->isFlying())
    {
        // Can't target it.
        return 0;
    }




    /* CODE ADDED */
    // Scout priorities go here
    if (rangedUnit->getType() == BWAPI::UnitTypes::Protoss_Scout) {
        // This is where the fun begins!


        if (!target->isFlying() && targetType.maxHitPoints() >= 500 && target->getHitPoints() >= 150) {
            // Scouts have low ground damage. There is no way we kill something on the ground with that much health
            return 1;
        }

        // Flying buildings aren't worth it
        if (target->getType().isFlyingBuilding()) {
            return 2;
        }
        

        // Harass workers, especially if they're in the middle of something
        if (targetType.isWorker()) {
            if (target->isRepairing() || target->isConstructing()) {
                return 12;
            }
            return 10;
        }


        // Unfinished static defenses are a good target
        if (targetType.isBuilding() && target->isBeingConstructed()) {
            if (UnitUtil::CanAttackAir(target)) {
                return 11;
            }
        }
        
        // Scouts are a part of DTDrop strat so this could be useful
        if (targetType.isDetector()) {
            return 10;
        }

        // Harass things that can't shoot back at us
        if (UnitUtil::CanAttackGround(target) && !UnitUtil::CanAttackAir(target)) {
            return 9;
        }
    }

    if (target->isStasised() || target->isMaelstrommed() || target->isUnderDisruptionWeb()) {
        if (UnitUtil::CanAttack(rangedUnit, target)) {
            return 1;
        }
    }

    // A carrier should not target an enemy interceptor. It's too hard to hit.
    if (rangedType == BWAPI::UnitTypes::Protoss_Carrier && targetType == BWAPI::UnitTypes::Protoss_Interceptor)
    {
        return 0;
    }

    // An addon other than a completed comsat is boring.
    // TODO should also check that it is attached
    if (targetType.isAddon() && !(targetType == BWAPI::UnitTypes::Terran_Comsat_Station && target->isCompleted()))
    {
        return 1;
    }

    // A ghost which is nuking is the highest priority by a mile.
    if (targetType == BWAPI::UnitTypes::Terran_Ghost &&
        target->getOrder() == BWAPI::Orders::NukePaint ||
        target->getOrder() == BWAPI::Orders::NukeTrack)
    {
        return 15;
    }

    // if the target is building something near our base, something is fishy
    BWAPI::Position ourBasePosition = BWAPI::Position(the.bases.myMain()->getPosition());
    if (target->getDistance(ourBasePosition) < 1000) {
        if (target->getType().isWorker() && (target->isConstructing() || target->isRepairing()))
        {
            return 12;
        }
        if (target->getType().isBuilding())
        {
            // This includes proxy buildings, which deserve high priority.
            // But when bases are close together, it can include innocent buildings.
            // We also don't want to disrupt priorities in case of proxy buildings
            // supported by units; we may want to target the units first.
            if (UnitUtil::CanAttackGround(target) || UnitUtil::CanAttackAir(target))
            {
                return 10;
            }
            return 8;
        }
    }
    
    if (rangedType.isFlyer()) {
        // Exceptions if we're a flyer.
        if (targetType == BWAPI::UnitTypes::Zerg_Scourge)
        {
            return 12;
        }
    }
    else
    {
        // Exceptions if we're a ground unit.
        if (targetType == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine && !target->isBurrowed() ||
            targetType == BWAPI::UnitTypes::Zerg_Infested_Terran)
        {
            return 12;
        }
    }

    // Wraiths, scouts, and goliaths strongly prefer air targets because they do more damage to air units.
    if (rangedType == BWAPI::UnitTypes::Terran_Wraith ||
        rangedType == BWAPI::UnitTypes::Protoss_Scout)
    {
        if (target->getType().isFlyer())    // air units, not floating buildings
        {
            return 11;
        }
    }
    else if (rangedType == BWAPI::UnitTypes::Terran_Goliath)
    {
        if (targetType.isFlyer())    // air units, not floating buildings
        {
            return 10;
        }
    }


    /* CODE ADDED */
    bool isUsingArbiters = the.my.completed.count(BWAPI::UnitTypes::Protoss_Arbiter_Tribunal) > 0;
    if (rangedType == BWAPI::UnitTypes::Protoss_Carrier) {
        if (
            // Terran:
            targetType == BWAPI::UnitTypes::Terran_Goliath 
         || targetType == BWAPI::UnitTypes::Terran_Missile_Turret
         || targetType == BWAPI::UnitTypes::Terran_Ghost
         || (targetType.isWorker() && (target->isConstructing() || target->isRepairing()) && (target->getBuildType() == BWAPI::UnitTypes::Terran_Missile_Turret))
            // Protoss:
         || targetType == BWAPI::UnitTypes::Protoss_Dark_Archon
         || targetType == BWAPI::UnitTypes::Protoss_Photon_Cannon
            // Zerg:
         || targetType == BWAPI::UnitTypes::Zerg_Hydralisk
         || targetType == BWAPI::UnitTypes::Zerg_Spore_Colony) {
            return 12;      // Prioritize anything that shoots air or impactful small targets
        }
        else if (
            // Terran:
            targetType == BWAPI::UnitTypes::Terran_Armory
            || targetType == BWAPI::UnitTypes::Terran_Medic
            || targetType == BWAPI::UnitTypes::Terran_Wraith
            || (targetType.isWorker() && target->isConstructing() && (target->getBuildType() == BWAPI::UnitTypes::Terran_Armory))
            // Protoss:
            || targetType == BWAPI::UnitTypes::Protoss_Dragoon
            || targetType == BWAPI::UnitTypes::Protoss_High_Templar
            // Zerg:
            || targetType == BWAPI::UnitTypes::Zerg_Mutalisk
            // All:
            || (isUsingArbiters && targetType.isDetector())
            ) {
            return 11;      // Destroy important supporting units and means of producing goliaths
        }
        else if (targetType == BWAPI::UnitTypes::Terran_Marine
            // SCVs repairing tanks:
             || (targetType.isWorker() && target->isRepairing() && target->getTarget() != nullptr
                 && (target->getTarget()->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode
                  || target->getTarget()->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode))

            || targetType == BWAPI::UnitTypes::Protoss_Dark_Templar

            || targetType == BWAPI::UnitTypes::Zerg_Defiler)
        {
            return 10;
        }
        else if (targetType.isWorker()) {
            return 9;
        }
        else if (targetType.isResourceDepot()) {
            return 8;
        }
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
    if (UnitUtil::CanAttack(targetType, rangedType) && !targetType.isWorker())
    {
        // Enemy unit which is far enough outside its range is lower priority than a worker.
        if (rangedUnit->getDistance(target) > 48 + UnitUtil::GetAttackRange(target, rangedUnit))
        {
            return 8;
        }
        return 10;
    }
    // Droppers are as bad as threats. They may be loaded and are often isolated and safer to attack.
    if (targetType == BWAPI::UnitTypes::Terran_Dropship ||
        targetType == BWAPI::UnitTypes::Protoss_Shuttle)
    {
        return 10;
    }
    // Also as bad are other dangerous things.
    if (targetType == BWAPI::UnitTypes::Terran_Science_Vessel ||
        targetType == BWAPI::UnitTypes::Zerg_Scourge)
    {
        return 10;
    }
    if (targetType == BWAPI::UnitTypes::Protoss_Observer)
    {
        // If we have cloaked units, observers are worse than threats.
        if (InformationManager::Instance().weHaveCloakTech())
        {
            return 11;
        }
        // Otherwise, they are equal.
        return 10;
    }
    // Next are workers.
    if (targetType.isWorker()) 
    {
        if (rangedUnit->getType() == BWAPI::UnitTypes::Terran_Vulture)
        {
            return 11;
        }
        // Repairing or blocking a choke makes you critical.
        if (target->isRepairing() || unitNearChokepoint(target))
        {
            return 11;
        }
        // SCVs constructing are also important.
        if (target->isConstructing())
        {
            return 10;
        }

        return 9;
    }

    // Important combat units that we may not have targeted above.
    if (targetType == BWAPI::UnitTypes::Protoss_Carrier)
    {
        return 8;
    }

    return getBackstopAttackPriority(target);
}

// Should the unit stay (or return) home until ready to move out?
bool MicroRanged::stayHomeUntilReady(const BWAPI::Unit u) const
{
    return
        u->getType() == BWAPI::UnitTypes::Protoss_Carrier && u->getInterceptorCount() < 3;  // CODE CHANGED
                                                                                            // If we're under attack, we can use the interceptors sooner
                                                                                            // If we're not, we can manufacture more on the way
}
