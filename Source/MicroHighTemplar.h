#pragma once

#include "Common.h"

namespace UAlbertaBot
{
class MicroManager;

class MicroHighTemplar : public MicroManager
{
private:
    std::unordered_map<BWAPI::Unit, int> _previousEnergy;
    int _lastStormCast = 0;
public:

    MicroHighTemplar();
    void executeMicro(const BWAPI::Unitset& targets, const UnitCluster& cluster);
    void assignTargets(const BWAPI::Unitset & hts, const BWAPI::Unitset & targets);
    void update();
    bool shouldRun(BWAPI::Unit ht, const BWAPI::Unitset& targets);
    BWAPI::Position getBestStormPosition(const BWAPI::Unit& ht, const BWAPI::Unitset& targets);
    int getStormValue(BWAPI::Unit u);
};
}