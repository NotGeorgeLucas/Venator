#include "EnemyScoutFollower.h"
#include "The.h"
#include "Bases.h"

/* CODE ADDED */
// The whole file

namespace UAlbertaBot {


BWAPI::Unit EnemyScoutFollower::getScout() const {
    if (!_scout) {
        return nullptr;
    }

    if (!_scout->exists()) {
        return nullptr;
    }

    return _scout;
}


const int MaxPathPoints = 150;
void EnemyScoutFollower::update() {
    // Try to check for worker scout running cirlces around base
    if (!the.bases.myMain()) { return; }


    const BWAPI::Position baseCenter = the.bases.myMain()->getCenter();

    // Keep current scout if possible
    if (_scout && _scout->exists()) {
        if (_scout->isVisible()) {
            const BWAPI::Position currentPos = _scout->getPosition();

            if (_scoutPath.empty()) {
                _scoutPath.push_back({ currentPos, 0.0 });
                return;
            }

            const BWAPI::Position& lastPos = _scoutPath.back().pos;
            if (currentPos.getDistance(lastPos) <= 48) {
                return;
            }

            if (_scoutPath.size() == 1) {
                _scoutPath.push_back({ currentPos, 0.0 });
                return;
            }

            const BWAPI::Position& prevPos = _scoutPath[_scoutPath.size() - 2].pos;
            const double turnAngle = getTurnAngleDeg(prevPos, lastPos, currentPos);

            _scoutPath.push_back({ currentPos, turnAngle });

            while (_scoutPath.size() > MaxPathPoints) {
                _scoutPath.erase(_scoutPath.begin());
            }
        }

        return; // unseen scout: keep old path, do nothing
    }

    // Reacquire only the same scout if we have its id
    BWAPI::Unitset targetsNearBase = BWAPI::Broodwar->getUnitsInRadius(baseCenter, 17 * 32, BWAPI::Filter::IsEnemy && !BWAPI::Filter::IsNeutral);

    for (BWAPI::Unit unit : targetsNearBase) {
        if (!unit || !unit->exists() || !unit->getType().isWorker()) continue;

        if (_scoutID != -1 && unit->getID() != _scoutID) {
            continue;
        }

        _scout = unit;
        _scoutID = unit->getID();

        if (_scoutPath.empty()) {
            _scoutPath.push_back({ unit->getPosition(), 0.0 });
        }

        return;
    }


    
}


double EnemyScoutFollower::getTurnAngleDeg(const BWAPI::Position & a, const BWAPI::Position & b, const BWAPI::Position & c) {
    const double v1x = double(b.x - a.x);
    const double v1y = double(b.y - a.y);
    const double v2x = double(c.x - b.x);
    const double v2y = double(c.y - b.y);

    const double len1 = std::hypot(v1x, v1y);
    const double len2 = std::hypot(v2x, v2y);

    if (len1 < 1.0 || len2 < 1.0) {
        return 0.0;
    }

    double cosTheta = (v1x * v2x + v1y * v2y) / (len1 * len2);
    cosTheta = std::clamp(cosTheta, -1.0, 1.0);

    return std::acos(cosTheta) * (180.0 / 3.14159265358979323846);
}


void EnemyScoutFollower::draw() {
    if (_scoutPath.size() < 2) {
        return;
    }

    for (size_t i = 1; i < _scoutPath.size(); i++) {
        const auto& a = _scoutPath[i - 1];
        const auto& b = _scoutPath[i];

        BWAPI::Broodwar->drawLineMap(
            a.pos.x,
            a.pos.y,
            b.pos.x,
            b.pos.y,
            BWAPI::Colors::Orange);
    }

    // draw nodes
    for (const auto& p : _scoutPath) {
        BWAPI::Broodwar->drawCircleMap(
            p.pos.x,
            p.pos.y,
            4,
            p.angle >= 120.0 ? BWAPI::Colors::Red : BWAPI::Colors::Yellow,
            true);
    }

    if (_scout && _scout->exists()) {
        BWAPI::Broodwar->drawTextMap(
            _scout->getPosition(),
            "Scout Path (%d pts)\n Loop: %d\nBack-Forth: %d",
            (int)_scoutPath.size(), isPathLooped() ? 1 : 0, isPathBackForth() ? 1 : 0);
    }

    BWAPI::Position catchPos = getScoutTrapPosition(3 * 32);

    if (catchPos != BWAPI::Positions::None) {
        BWAPI::Broodwar->drawCircleMap(catchPos, 2 * 32, BWAPI::Colors::Red); 
    }
}


bool EnemyScoutFollower::isPathLooped() {
    if (_scoutPath.size() < 20) return false;

    const auto& last = _scoutPath.back().pos;

    constexpr double loopRadius = 96.0;

    for (size_t i = 0; i + 10 < _scoutPath.size(); i++) {
        if (last.getDistance(_scoutPath[i].pos) < loopRadius) {
            return true;
        }
    }

    return false;
}


bool EnemyScoutFollower::isPathBackForth() {
    int flipCount = 0;

    for (const auto& p : _scoutPath) {
        flipCount += p.angle >= 120.0;

        if (flipCount >= 2) return true;
    }

    return false;
}


BWAPI::Position EnemyScoutFollower::getScoutTrapPosition(int predictionFrames) {

    if (!_scout || !_scout->exists()) return BWAPI::Positions::None;

    if (_lastScoutCheck == the.now()) {
        return _lastTrapPos;
    }

    _lastScoutCheck = the.now();

    if (isPathBackForth()) {
        std::vector<PathEntry> loopSegment;

        bool foundLoop = false;
        for (const auto& p : _scoutPath) {
            if (p.pos.getApproxDistance(the.bases.myMain()->getCenter()) >= 17 * 32) continue;
            if (p.angle >= 120.0) {
                if (!foundLoop) {
                    foundLoop = true;
                }
                else {
                    break;
                }
            }
            if (foundLoop) loopSegment.push_back(p);

        }
        _lastTrapPos = (foundLoop && loopSegment.size() >= 5) ? loopSegment[loopSegment.size() / 2].pos : BWAPI::Positions::None;

        return _lastTrapPos;
    }
    else if (isPathLooped()) {

        BWAPI::Position bestPos = BWAPI::Positions::None;
        int bestDist = INT_MIN;

        BWAPI::Unitset minerals = the.bases.myMain()->getMinerals();
        BWAPI::Unitset geysers = the.bases.myMain()->getGeysers();

        BWAPI::Position avgMinerals = UnitUtil::getAveragePosition(the.bases.myMain()->getMinerals());
        BWAPI::Position avgGeysers = UnitUtil::getAveragePosition(the.bases.myMain()->getGeysers());

        // As far away from the minerals and gas as we can to avoid bumping with probes. Also tends to be towards the exit to make the chase easier
        for (const auto& p : _scoutPath) {
            if (p.pos.getApproxDistance(the.bases.myMain()->getCenter()) >= 17 * 32) continue;
            int score = avgGeysers.getApproxDistance(p.pos) + avgMinerals.getApproxDistance(p.pos);

            if (score > bestDist) {
                bestDist = score;
                bestPos = p.pos;
            }
        }

        _lastTrapPos = bestPos;

        return bestPos;
    }
    else {
        _lastTrapPos = BWAPI::Positions::None;
        return BWAPI::Positions::None;
    }
}

// Not used but may turn out useful idk
BWAPI::Position EnemyScoutFollower::predictPositionInNFrames(int predictionFrames) {

    if (!_scout || !_scout->exists() || _scoutPath.size() < 2) {
        return BWAPI::Positions::None;
    }

    const BWAPI::Position currentPos = _scout->getPosition();

    // Find closest recorded path point to current scout position
    size_t closestIndex = 0;
    double closestDist = DBL_MAX;

    for (size_t i = 0; i < _scoutPath.size(); ++i) {
        const double dist = currentPos.getDistance(_scoutPath[i].pos);

        if (dist < closestDist) {
            closestDist = dist;
            closestIndex = i;
        }
    }


    double speed = _scout->getType().topSpeed();
    if (speed <= 0.0) {
        speed = 3.0;
    }

    const double futureDistance = speed * predictionFrames;

    double accumulatedDistance = 0.0;
    size_t predictedIndex = closestIndex;

    while (accumulatedDistance < futureDistance) {
        const size_t nextIndex =
            (predictedIndex + 1) % _scoutPath.size();

        accumulatedDistance +=
            _scoutPath[predictedIndex].pos.getDistance(
                _scoutPath[nextIndex].pos);

        predictedIndex = nextIndex;

        // avoid infinite loop if path is tiny
        if (predictedIndex == closestIndex) {
            break;
        }
    }

    return _scoutPath[predictedIndex].pos;
}

}