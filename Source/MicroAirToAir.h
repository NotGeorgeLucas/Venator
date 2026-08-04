#pragma once

#include "MicroManager.h"
#include "InformationManager.h"
#include "UnitUtil.h"
#include <cmath>

namespace UAlbertaBot
{
class MicroManager;

class MicroAirToAir : public MicroManager
{
private:
    int _lastDWebFrame = 0;

    struct Threat {
        BWAPI::Position currPos;
        BWAPI::Position predictedPos;
        int range;
        BWAPI::UnitType type;
        bool isFinished = true;

        Threat() = default;

        Threat(const UnitInfo& ui, BWAPI::Unit target, int predictFrames = 6) {

            if (!ui.unit || !ui.unit->exists())
                return;

            BWAPI::Unit u = ui.unit;

            BWAPI::Position curr;
            if (u->isVisible()) curr = u->getPosition();
            else if (ui.lastPosition.isValid()) curr = ui.lastPosition;
            else curr = BWAPI::Position(0, 0); // worst-case fallback

            currPos = curr;

            BWAPI::Position velocity(0, 0);
            if (u->isVisible()) {
                velocity = BWAPI::Position(int(u->getVelocityX() * predictFrames), int(u->getVelocityY() * predictFrames));
            }
            else {
                velocity = BWAPI::Position(0, 0);
            }

            predictedPos = currPos + velocity;

            range = UnitUtil::GetAttackRange(u, target) + 32;

            type = ui.type;

            if (type.isBuilding() && !u->isCompleted()) isFinished = false;
        }
    };


public:
    std::vector<Threat> computeThreats(BWAPI::Unit & airUnit, const std::map<BWAPI::Unit, UnitInfo> & enemyForce) const;


	MicroAirToAir();
	void executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster);

	void assignTargets(const BWAPI::Unitset & airUnits, const BWAPI::Unitset & targets);
	BWAPI::Unit chooseTarget(BWAPI::Unit rangedUnit, const BWAPI::Unitset & targets, std::map<BWAPI::Unit, int> & numTargeting);

    void regroup(const BWAPI::Position& regroupPosition, const UnitCluster& cluster) const override;


    /* CODE ADDED */
    // Helper functions for pathing
    double dist2(double x1, double y1, double x2, double y2) {
        return (x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2);
    }

    // Check if line segment (x1,y1)-(x2,y2) overlaps circle (cx,cy,r)
    bool lineIntersectsCircle(double x1, double y1,
        double x2, double y2,
        double cx, double cy,
        double r) {
        // Vector from A to B
        double dx = x2 - x1;
        double dy = y2 - y1;

        double lenSq = dx * dx + dy * dy;

        // Handle degenerate segment (point)
        if (lenSq < 1e-6) {
            return dist2(x1, y1, cx, cy) <= r * r;
        }


        // Vector from A to circle center
        double fx = cx - x1;
        double fy = cy - y1;

        // Project center onto the segment, get parameter t
        double t = (fx * dx + fy * dy) / lenSq;

        // Clamp t to segment [0,1]
        if (t < 0.0) t = 0.0;
        else if (t > 1.0) t = 1.0;

        // Closest point on segment
        double closestX = x1 + t * dx;
        double closestY = y1 + t * dy;

        // Check distance to circle center
        return dist2(closestX, closestY, cx, cy) <= r * r;
    }

    // Returns a perpendicular displacement of the given vector
    BWAPI::Position perpendicularOffset(const BWAPI::Position& v, float displace) {
        // Perpendicular vector (rotate 90 degrees)
        float px = (float)  - v.y;
        float py = (float) v.x;

        // Length of perpendicular vector
        float len = std::sqrt(px * px + py * py);

        // Avoid division by zero (aka your vector has no personality)
        if (len == 0.0f) {
            return BWAPI::Position(0, 0);
        }

        // Normalize and scale
        px = (px / len) * displace;
        py = (py / len) * displace;

        return BWAPI::Position(static_cast<int>(px), static_cast<int>(py));
    }


    // Returns a radial offset of the given vector (same direction, fixed distance)
    BWAPI::Position radialOffset(const BWAPI::Position& v, float displace) {
        float x = (float)v.x;
        float y = (float)v.y;

        // Length of original vector
        float len = std::sqrt(x * x + y * y);

        // Vector has no direction, congrats on your philosophical void
        if (len == 0.0f) {
            return BWAPI::Position(0, 0);
        }

        // Normalize and scale to desired distance
        x = (x / len) * displace;
        y = (y / len) * displace;

        return BWAPI::Position(static_cast<int>(x), static_cast<int>(y));
    }



    bool canDWeb(BWAPI::Unit flyer) const {
        return flyer->getType() == BWAPI::UnitTypes::Protoss_Corsair
            && BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Disruption_Web)
            && flyer->getEnergy() >= 125;
    };


    bool hasGroundSupport(BWAPI::Unit unit) const {
        BWAPI::Unitset allies = unit->getUnitsInRadius(8 * 32, BWAPI::Filter::IsAlly && !BWAPI::Filter::IsNeutral && BWAPI::Filter::GroundWeapon != BWAPI::WeaponTypes::None);

        if (allies.size() >= 2) {

            BWAPI::Broodwar->drawTextMap(unit->getPosition() + BWAPI::Position(0, 48), "Support mode");
            return true;
        }

        BWAPI::Broodwar->drawTextMap(unit->getPosition() + BWAPI::Position(0, 48), "Standalone mode");
        return false;
    };


    bool pathSafe(const BWAPI::Position& from, const BWAPI::Position& to, const std::vector<Threat>& threatVector, BWAPI::Unit airUnit) {
        if (!from.isValid() || !to.isValid())
            return false;

        for (auto& threat : threatVector) {


            BWAPI::Position tp = threat.currPos;
            BWAPI::Position tpp = threat.predictedPos;

            // destination itself unsafe
            if (to.getDistance(tp) <= threat.range) return false;

            // path intersects threat zone
            if (lineIntersectsCircle(from.x, from.y, to.x, to.y, tp.x, tp.y, threat.range)) {
                return false;
            }

            // Same but for predicted pos
            if (to.getDistance(tpp) <= threat.range) return false;
            if (lineIntersectsCircle(from.x, from.y, to.x, to.y, tpp.x, tpp.y, threat.range)) { return false; }
        }

        return true;
    };


    bool isWithinNTilesOfMapEdge(BWAPI::Unit unit, int nTiles) const;
    BWAPI::Position nearestMapCorner(BWAPI::Position p);
    BWAPI::Position findCliffAwayFromCorner(BWAPI::Unit unit, BWAPI::Position corner);

    int getEnemyWebValue(BWAPI::Unit caster, BWAPI::Unit target);
    // For support webs
    BWAPI::Position getBestWebCast(BWAPI::Unit caster, const std::vector<UnitInfo>& targets);
    
    // For selfish webs
    BWAPI::Position netForAllThreats(const std::vector<Threat> threats);
    

    BWAPI::Unit pickNewHunter(const BWAPI::Unitset& airUnits, BWAPI::Unit exclude1, BWAPI::Unit exclude2, const BWAPI::Position& reference);
    void handleHunterPatrol(BWAPI::Unit airUnit, const BWAPI::Position& enemyBase, const BWAPI::Position& enemyNatural);

	int getAttackPriority(BWAPI::Unit rangedUnit, BWAPI::Unit target);
	BWAPI::Unit getTarget(BWAPI::Unit rangedUnit, const BWAPI::Unitset & targets);

};
}