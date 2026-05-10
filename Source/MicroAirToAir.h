#pragma once

#include "MicroManager.h"
#include <cmath>

namespace UAlbertaBot
{
class MicroManager;

class MicroAirToAir : public MicroManager
{
public:

	MicroAirToAir();
	void executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster);

	void assignTargets(const BWAPI::Unitset & airUnits, const BWAPI::Unitset & targets);
	BWAPI::Unit chooseTarget(BWAPI::Unit rangedUnit, const BWAPI::Unitset & targets, std::map<BWAPI::Unit, int> & numTargeting);



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


    BWAPI::Unit pickNewHunter(const BWAPI::Unitset& airUnits, BWAPI::Unit exclude1, BWAPI::Unit exclude2, const BWAPI::Position& reference);
    void handleHunterPatrol(BWAPI::Unit airUnit, const BWAPI::Position& enemyBase, const BWAPI::Position& enemyNatural);

	int getAttackPriority(BWAPI::Unit rangedUnit, BWAPI::Unit target);
	BWAPI::Unit getTarget(BWAPI::Unit rangedUnit, const BWAPI::Unitset & targets);

};
}