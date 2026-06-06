#pragma once

#include "MicroManager.h"

#include "The.h"
#include "Bases.h"

namespace UAlbertaBot
{
    class MicroArbiters : public MicroManager
    {
    private:
        int getAllyCloakValue(BWAPI::Unit ally);
        int getEnemyFreezeValue(BWAPI::Unit caster, BWAPI::Unit target);

        int getArbiterNum(const UnitCluster & cluster);

        struct Leash {
            BWAPI::Position center;
            BWAPI::Unitset units;
            int range;
        };

        enum ArbiterStatus {
            None,
            LookingToCloak,
            LookingToStasis
        };

        int _lastStasisCast = 0;
        std::map<int, Leash> _arbiterLeashes;
        std::map<BWAPI::Unit, ArbiterStatus> _unitStatusMap;
        std::vector<UnitCluster> _supportableClusters;



        std::map<int, int> lastSeenEnergy;
        std::map<int, int> energyByUnit;

    public:


        bool hasGroundSupport(BWAPI::Unit unit) const {
            BWAPI::Unitset allies = unit->getUnitsInRadius(8 * 32, BWAPI::Filter::IsAlly && !BWAPI::Filter::IsNeutral && BWAPI::Filter::CanAttack);

            return allies.size() >= 3;
        };

        bool canCastStasis(BWAPI::Unit arbiter) const {
            return arbiter->getType() == BWAPI::UnitTypes::Protoss_Arbiter
                && arbiter->getEnergy() >= BWAPI::TechTypes::Stasis_Field.energyCost()
                && the.self()->hasResearched(BWAPI::TechTypes::Stasis_Field);
        };

        void findBestSupportClusters(const std::vector<UnitCluster> clusters);

        MicroArbiters();

        void executeMicro(const BWAPI::Unitset& targets, const UnitCluster& cluster);
        void assignTargets(const BWAPI::Unitset& arbiterUnits, const BWAPI::Unitset& targets);

        BWAPI::Position getBestCloakPosition(const BWAPI::Unitset & allies, int radius);
        BWAPI::Position getBestStasisCast(BWAPI::Unit caster, const BWAPI::Unitset& targets);

        int  getAttackPriority(BWAPI::Unit arbiter, BWAPI::Unit target);
        BWAPI::Unit getTarget(BWAPI::Unit arbiter, const BWAPI::Unitset& targets, bool underThreat);

        void regroup(const BWAPI::Position& regroupPosition, const UnitCluster& cluster) const override;

        bool isOnRamp(BWAPI::Position pos) {

            BWAPI::TilePosition tile(pos);

            if (!tile.isValid()) { return false; }

            int h = BWAPI::Broodwar->getGroundHeight(tile);

            static const int dx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
            static const int dy[8] = { -1,-1,-1,  0, 0,  1, 1, 1 };

            for (int i = 0; i < 8; i++) {
                BWAPI::TilePosition n(tile.x + dx[i], tile.y + dy[i]);

                if (!n.isValid()) { continue; }

                if (BWAPI::Broodwar->getGroundHeight(n) != h) {
                    return true;
                }
            }

            return false;
        }
    };
}
