#pragma once
#include "Common.h"
#include "UnitUtil.h"

namespace UAlbertaBot {
	class EnemyScoutFollower
	{
	private:
		BWAPI::Unit _scout;
		int _scoutID = -1;
		int _lastScoutCheck = 0;
		BWAPI::Position _lastTrapPos = BWAPI::Positions::None;
	struct PathEntry {
		BWAPI::Position pos;
		double angle; // Degrees
	};
	std::vector<PathEntry> _scoutPath;
	
	double getTurnAngleDeg(const BWAPI::Position& a, const BWAPI::Position& b, const BWAPI::Position& c);
	static double normalizeAngle(double a) {
		while (a > 180.0)  a -= 360.0;
		while (a < -180.0) a += 360.0;
		return a;
	}

	public:
		
		
		void update();
		void draw();
		BWAPI::Position getScoutTrapPosition(int predictionFrames);
		BWAPI::Position predictPositionInNFrames(int predictionFrames);

		bool isPathLooped();
		bool isPathBackForth();
		BWAPI::Unit getScout() const;

	};
}

