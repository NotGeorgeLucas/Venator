#pragma once

#include "MicroManager.h"
#include "The.h"

namespace UAlbertaBot
{
    class MicroDarkArchon : public MicroManager
    {
    private:
        BWAPI::Position getBestMaelstromPosition(const BWAPI::Unitset& targets);
        BWAPI::Unit getBestFeedbackTarget(BWAPI::Unit da, const BWAPI::Unitset& targets);
        int getMaelstromValue(BWAPI::Unit u);

        bool shouldRun(BWAPI::Unit da, const BWAPI::Unitset& targets);

    public:
        MicroDarkArchon();

        void executeMicro(const BWAPI::Unitset& targets, const UnitCluster& cluster);
        void assignTargets(const BWAPI::Unitset& das, const BWAPI::Unitset& targets);
    };
}

