#pragma once

#include "MicroManager.h"

namespace UAlbertaBot
{
class MicroRanged : public MicroManager
{
private:

    // Ranged ground weapon does splash damage, so it works under dark swarm.
    bool goodUnderDarkSwarm(BWAPI::UnitType type);


    /* CODE ADDED */
    // Information about ranged carrier counters and if we can and should go highground
    struct CarrierThreatInfo {
        BWAPI::Unitset threats;
        int maxThreatElevation;
        bool shouldGoHighground;
    };

public:

    MicroRanged();

    void executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster);
    void assignTargets(const BWAPI::Unitset & rangedUnits, const BWAPI::Unitset & targets);

    void cleanupCarrierTargets();
    BWAPI::Position computeCarrierVector(BWAPI::Unit carrier, const std::vector<std::pair<BWAPI::Unit, int>>& targets);

    //void calculateWaypoints();
    //void followPerimeter(BWAPI::Unit flyer, BWAPI::Position target);
    //int waypointIndex(int i);
    //const BWAPI::Position& waypoint(int i);
    //void cleanupPersistentState();

    bool underBaseThreat();
    void doCarrierAttack(BWAPI::Unit carrier, BWAPI::Unit target, CarrierThreatInfo cInfo);
    bool shouldIssueNewOrder(BWAPI::Unit unit, BWAPI::Unit target);

    // Vector maths for seeing the approximate direction of where the enemy threatening units are
    BWAPI::Position averageNormalizedVector(const BWAPI::Unitset& units, BWAPI::Position origin) {
        double sumX = 0.0;
        double sumY = 0.0;

        for (const auto& unit : units)
        {
            if (!unit || !unit->exists())
                continue;

            BWAPI::Position dir = unit->getPosition() - origin;

            double x = static_cast<double>(dir.x);
            double y = static_cast<double>(dir.y);

            double len = std::sqrt(x * x + y * y);
            if (len > 0.0)
            {
                sumX += x / len;
                sumY += y / len;
            }
        }

        double sumLen = std::sqrt(sumX * sumX + sumY * sumY);
        if (sumLen == 0.0)
            return BWAPI::Position(0, 0);

        return BWAPI::Position(
            static_cast<int>(sumX / sumLen),
            static_cast<int>(sumY / sumLen)
        );
    }

    // Returns true if angle difference between vectors is bigger than the given value in degrees
    bool angleDifferenceGreaterThan(const BWAPI::Position& a, const BWAPI::Position& b, double thresholdDegrees) {
        double ax = static_cast<double>(a.x);
        double ay = static_cast<double>(a.y);
        double bx = static_cast<double>(b.x);
        double by = static_cast<double>(b.y);

        double dot = ax * bx + ay * by;

        double magA = std::sqrt(ax * ax + ay * ay);
        double magB = std::sqrt(bx * bx + by * by);

        if (magA == 0.0 || magB == 0.0)
            return false; // no meaningful angle

        double cosTheta = dot / (magA * magB);

        // clamp
        if (cosTheta > 1.0) cosTheta = 1.0;
        if (cosTheta < -1.0) cosTheta = -1.0;

        double thetaRad = std::acos(cosTheta);
        double thetaDeg = thetaRad * (180.0 / 3.14159265358979323846);

        return thetaDeg > thresholdDegrees;
    }


    int getAttackPriority(BWAPI::Unit rangedUnit, BWAPI::Unit target);
    BWAPI::Unit getTarget(BWAPI::Unit rangedUnit, const BWAPI::Unitset & targets, bool underThreat);

    bool stayHomeUntilReady(const BWAPI::Unit u) const;
};
}