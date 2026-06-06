#include "MicroDarkArchon.h"
#include "UnitUtil.h"
#include "The.h"
#include "Bases.h"

using namespace UAlbertaBot;

MicroDarkArchon::MicroDarkArchon() {}


void MicroDarkArchon::executeMicro(const BWAPI::Unitset& targets, const UnitCluster& cluster) {
    BWAPI::Unitset das = Intersection(getUnits(), cluster.units);
    if (das.empty()) return;

    assignTargets(das, targets);
}


void MicroDarkArchon::assignTargets(const BWAPI::Unitset& das, const BWAPI::Unitset& targets) {
    for (BWAPI::Unit da : das) {
        if (!da || !da->exists()) continue;

        if (shouldRun(da, targets)) {
            the.micro.Move(da, the.bases.myMain()->getPosition());
            continue;
        }

        BWAPI::Position msTarget = getBestMaelstromPosition(targets);
        if (msTarget && da->getEnergy() >= 100) {
            da->useTech(BWAPI::TechTypes::Maelstrom, msTarget);
            continue;
        }

        BWAPI::Unit fbTarget = getBestFeedbackTarget(da, targets);
        if (fbTarget && da->getEnergy() >= 50) {
            da->useTech(BWAPI::TechTypes::Feedback, fbTarget);
            continue;
        }

        // default: just follow army
        the.micro.MoveNear(da, order->getPosition());
    }
}


bool MicroDarkArchon::shouldRun(BWAPI::Unit da, const BWAPI::Unitset& targets) {
    for (BWAPI::Unit u : targets) {
        if (!u || !u->exists()) continue;

        if (u->getType().isDetector()) continue;

        if (u->getDistance(da) < 5 * 32 && UnitUtil::CanAttack(u, da)) {
            return true;
        }
    }

    return false;
}


BWAPI::Unit MicroDarkArchon::getBestFeedbackTarget(BWAPI::Unit da, const BWAPI::Unitset& targets) {
    int bestScore = 0;
    BWAPI::Unit best = nullptr;

    for (BWAPI::Unit u : targets) {
        if (!u || !u->exists()) continue;
        if (u->getPlayer() == BWAPI::Broodwar->self()) continue;

        int score = 0;

        if (u->getType() == BWAPI::UnitTypes::Protoss_High_Templar) score += 100;
        if (u->getType() == BWAPI::UnitTypes::Terran_Science_Vessel) score += 80;
        if (u->getType().isSpellcaster()) score += 40;

        if (score > bestScore) {
            bestScore = score;
            best = u;
        }
    }

    return bestScore > 50 ? best : nullptr;
}


BWAPI::Position MicroDarkArchon::getBestMaelstromPosition(const BWAPI::Unitset& targets) {
    if (targets.empty()) return BWAPI::Positions::None;

    int bestScore = std::numeric_limits<int>::min();
    BWAPI::Position bestPos = BWAPI::Positions::Invalid;

    const int radius = 96; // actually a matrix but whatever

    for (const BWAPI::Unit& centerUnit : targets) {
        if (!centerUnit || !centerUnit->exists()) continue;
        if (centerUnit->getType().isBuilding()) continue;
        if (!centerUnit->getType().isOrganic()) continue;

        BWAPI::Position center = centerUnit->getPosition();

        int left = center.x - radius / 2;
        int right = center.x + radius / 2;
        int top = center.y - radius / 2;
        int bottom = center.y + radius / 2;

        int score = 0;

        for (const BWAPI::Unit& u : targets) {
            if (!u || !u->exists()) continue;
            if (!u->getType().isOrganic()) continue;
            if (u->isMaelstrommed() || u->isLockedDown()) continue;

            BWAPI::Position p = u->getPosition();

            if (p.x >= left && p.x <= right && p.y >= top && p.y <= bottom) {
                score += getMaelstromValue(u);
            }
        }

        if (score > bestScore) {
            bestScore = score;
            bestPos = center;
        }
    }

    return bestScore >= 40 ? bestPos : BWAPI::Positions::None;
}


int MicroDarkArchon::getMaelstromValue(BWAPI::Unit u) {
    if (!u || !u->exists()) return 0;

    if (!u->getType().isOrganic()) return 0;
    if (u->isStasised() || u->isMaelstrommed() || u->isUnderDisruptionWeb()) return 0;

    using namespace BWAPI::UnitTypes;

    int score = 0;
    BWAPI::UnitType type = u->getType();


    if (type == Zerg_Defiler)           score += 100;
    else if (type == Zerg_Queen)        score += 90;
    else if (type == Zerg_Lurker)       score += 70;
    else if (type == Zerg_Mutalisk)     score += 65;
    else if (type == Zerg_Hydralisk)    score += 55;
    else if (type == Zerg_Ultralisk)    score += 60;
    else if (type == Zerg_Zergling)     score += 25;

    // tech / spellcasters
    if (type.maxEnergy() > 0) score += 20;

    // economic value
    score += type.mineralPrice() / 50;
    score += type.gasPrice() / 50;

    // if it’s actively fighting, better freeze timing
    if (u->isAttacking()) score += 10;

    return score;
}