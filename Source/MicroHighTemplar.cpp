#include "MicroManager.h"
#include "MicroHighTemplar.h"

#include "Bases.h"
#include "The.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// For now, all this does is immediately merge high templar into archons.

MicroHighTemplar::MicroHighTemplar()
{ 
}

void MicroHighTemplar::update()
{

    for (BWAPI::Unit ht : getUnits()) {
        if (!ht || !ht->exists())
            continue;

        int currentEnergy = ht->getEnergy();

        auto it = _previousEnergy.find(ht);

        if (it != _previousEnergy.end()) {
            int energyDifference = it->second - currentEnergy;

            // 60 less energy means we definitely cast storm
            if (energyDifference >= 60) {
                _lastStormCast = the.now();
            }
        }

        _previousEnergy[ht] = currentEnergy;
    }

    if (the.enemyRace() == BWAPI::Races::Zerg) {

        if (getUnits().size() < 2)
        {
            // Takes 2 high templar to merge one archon.
            return;
        }

        // No base should be tight against an edge, so this position should always be reachable.
        const BWAPI::Position gatherPoint =
            the.bases.myMain()->getPosition() - BWAPI::Position(32, 32);
        UAB_ASSERT(gatherPoint.isValid(), "bad gather point");

        BWAPI::Unitset mergeGroup;

        for (const auto templar : getUnits())
        {
            const int framesSinceCommand = BWAPI::Broodwar->getFrameCount() - templar->getLastCommandFrame();
            const bool longEnough = framesSinceCommand >= 12;

            if (templar->getLastCommand().getType() == BWAPI::UnitCommandTypes::Use_Tech_Unit && !longEnough)
            {
                // Wait. There's latency before the command takes effect.
            }
            else if (templar->getOrder() == BWAPI::Orders::ArchonWarp && framesSinceCommand > 5 * 24)
            {
                // The merge has been going on too long. It may be stuck. Stop and try again.
                the.micro.Move(templar, gatherPoint);
            }
            else if (templar->getLastCommand().getType() == BWAPI::UnitCommandTypes::Use_Tech_Unit && !longEnough)
            {
                // Keep waiting.
            }
            else if (templar->getOrder() == BWAPI::Orders::PlayerGuard)
            {
                mergeGroup.insert(templar);
            }
            else if (templar->getOrder() != BWAPI::Orders::ArchonWarp)
            {
                if (templar->getDistance(gatherPoint) >= 3 * 32)
                {
                    // Join up before trying to merge.
                    the.micro.Move(templar, gatherPoint);
                }
                else
                {
                    the.micro.Stop(templar);
                }
            }
        }

        // We will merge 1 pair per call, the pair closest together.
        int closestDist = MAX_DISTANCE;
        BWAPI::Unit closest1 = nullptr;
        BWAPI::Unit closest2 = nullptr;

        for (const auto ht1 : mergeGroup)
        {
            for (const auto ht2 : mergeGroup)
            {
                if (ht2 == ht1)    // loop through all ht2 until we reach ht1
                {
                    break;
                }
                int dist = ht1->getDistance(ht2);
                if (dist < closestDist)
                {
                    closestDist = dist;
                    closest1 = ht1;
                    closest2 = ht2;
                }
            }
        }

        if (closest1)
        {
            (void) the.micro.MergeArchon(closest1, closest2);
        }
    }
    else {
        /* CODE ADDED */
        // Rely on actives in non-PvZ
    }

}


void MicroHighTemplar::executeMicro(const BWAPI::Unitset& targets, const UnitCluster& cluster) {
    BWAPI::Unitset hts = Intersection(getUnits(), cluster.units);

    if (hts.empty()) {
        return;
    }

    assignTargets(hts, targets);
}


void MicroHighTemplar::assignTargets(const BWAPI::Unitset& hts, const BWAPI::Unitset& targets) {
    for (BWAPI::Unit ht : hts) {
        if (!ht || !ht->exists()) continue;

        if (shouldRun(ht, targets)) {
            the.micro.Move(ht, the.bases.myMain()->getPosition());
            continue;
        }

        if (the.self()->hasResearched(BWAPI::TechTypes::Psionic_Storm) && ht->getEnergy() >= 75) {
            if (the.now() - _lastStormCast >= 24) {
                BWAPI::Position stormTargetPoint = getBestStormPosition(ht, targets);
                if (stormTargetPoint != BWAPI::Positions::None) {
                    ht->useTech(BWAPI::TechTypes::Psionic_Storm, stormTargetPoint);
                    BWAPI::Broodwar->drawBoxMap(stormTargetPoint - BWAPI::Position(48, 48), stormTargetPoint + BWAPI::Position(48, 48), BWAPI::Colors::Blue);
                    if (ht->getDistance(stormTargetPoint) < 10 * 32) {
                    }
                }
            }
        }

        // default: just follow army
        the.micro.MoveNear(ht, order->getPosition());
    }
}


bool MicroHighTemplar::shouldRun(BWAPI::Unit ht, const BWAPI::Unitset& targets) {

    if (!the.self()->hasResearched(BWAPI::TechTypes::Psionic_Storm) && !the.self()->hasResearched(BWAPI::TechTypes::Hallucination) && the.enemyRace() != BWAPI::Races::Zerg) return true;

    for (BWAPI::Unit u : targets) {
        if (!u || !u->exists()) continue;

        if (u->getType().isDetector()) continue;

        if (u->getDistance(ht) < 6 * 32 && UnitUtil::CanAttack(u, ht)) {
            return true;
        }
    }

    return false;
}


BWAPI::Position MicroHighTemplar::getBestStormPosition(const BWAPI::Unit & ht, const BWAPI::Unitset& targets) {
    if (targets.empty()) return BWAPI::Positions::None;

    int bestScore = std::numeric_limits<int>::min();
    BWAPI::Position bestPos = BWAPI::Positions::None;

    for (BWAPI::Unit centerUnit : targets) {
        if (!centerUnit || !centerUnit->exists())
            continue;

        if (centerUnit->getType().isBuilding())
            continue;

        BWAPI::Position center = centerUnit->getPosition();

        int score = 0;

        for (BWAPI::Unit u : targets) {
            if (!u || !u->exists())
                continue;

            if ((std::abs(u->getPosition().x - center.x) > 48) && (std::abs(u->getPosition().y - center.y) > 48))
                continue;

            int unitScore = getStormValue(u);
            if (u->getDistance(ht) <= 10 * 32 + 16) {
                unitScore *= 1.5;
            }
            score += unitScore;
        }

        for (BWAPI::Unit u : BWAPI::Broodwar->getUnitsInRadius(center, 70, BWAPI::Filter::IsAlly)) {
            if ((std::abs(u->getPosition().x - center.x) > 48) && (std::abs(u->getPosition().y - center.y) > 48) && u->getType() != BWAPI::UnitTypes::Protoss_Interceptor)
                continue;
            score -= 67;
        }

        if (score > bestScore) {
            bestScore = score;
            bestPos = center;
        }
    }

    // Don't waste storms.
    return bestScore >= 80 ? bestPos : BWAPI::Positions::None;
}


int MicroHighTemplar::getStormValue(BWAPI::Unit u) {
    if (!u || !u->exists())
        return 0;

    if (u->isStasised())
        return 0;

    using namespace BWAPI::UnitTypes;

    BWAPI::UnitType type = u->getType();
    int score = 0;

    // Spellcasters
    if (type == Terran_Science_Vessel) score += 90;
    else if (type == Protoss_High_Templar) score += 90;
    else if (type == Zerg_Defiler) score += 100;
    else if (type == Zerg_Queen) score += 80;

    // Terran bio
    else if (type == Terran_Marine) score += 45;
    else if (type == Terran_Medic) score += 55;
    else if (type == Terran_Firebat) score += 25;
    else if (type == Terran_Ghost) score += 55;

    // Terran mech
    else if (type == Terran_Goliath) score += 130;  // The whole reason why I make HT micro

    // Zerg (TODO: just a placeholder since we use archons in PvZ)
    else if (type == Zerg_Hydralisk) score += 45;
    else if (type == Zerg_Mutalisk) score += 40;
    else if (type == Zerg_Lurker) score += 70;
    else if (type == Zerg_Zergling) score += 18;
    else if (type == Zerg_Ultralisk) score += 25;

    // Protoss
    else if (type == Protoss_Dragoon) score += 25;
    else if (type == Protoss_Zealot) score += 20;
    else if (type == Protoss_Dark_Templar) score += 60;
    else if (type == Protoss_Archon) score += 30;
    else if (type == Protoss_Reaver) score += 80;

    // Workers
    else if (type.isWorker()) score += 10;

    // Generic value scaling
    score += type.mineralPrice() / 50;
    score += type.gasPrice() / 50;

    // Bonus if actively fighting
    if (u->isAttacking())
        score += 10;

    // Bonus for low HP units that Storm is likely to finish
    if (u->getHitPoints() < 70)
        score += 10;

    bool inUnderStorm = false;

    for (BWAPI::Bullet bullet : BWAPI::Broodwar->getBullets()) {
        if (!bullet->exists())
            continue;

        if (bullet->getPlayer() != BWAPI::Broodwar->self())
            continue;

        if (bullet->getType() != BWAPI::BulletTypes::Psionic_Storm)
            continue;

        BWAPI::Position stormPos = bullet->getPosition();

        if ((std::abs(u->getPosition().x - stormPos.x) > 48) && (std::abs(u->getPosition().y - stormPos.y) > 48)) {
            continue;
        }
        else {
            inUnderStorm = true;
        }
    }

    if (!inUnderStorm) {
        return score;
    }
    else {
        return -score;
    }
}