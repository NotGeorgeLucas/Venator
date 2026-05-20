#pragma once

#include "MicroManager.h"

#include "The.h"
#include "Bases.h"

namespace UAlbertaBot
{
    class MicroArbiters : public MicroManager
    {
    private:

    public:

        MicroArbiters();

        void executeMicro(const BWAPI::Unitset& targets, const UnitCluster& cluster);
        void assignTargets(const BWAPI::Unitset& arbiterUnits, const BWAPI::Unitset& targets);

        int  getAttackPriority(BWAPI::Unit arbiter, BWAPI::Unit target);
        BWAPI::Unit getTarget(BWAPI::Unit arbiter, const BWAPI::Unitset& targets, bool underThreat);
    };
}
