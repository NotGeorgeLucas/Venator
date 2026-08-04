#pragma once

#include "BuildingData.h"
#include "GridBuildable.h"

namespace UAlbertaBot
{
class The;
class Base;

class BuildingPlacer
{
    std::vector< std::vector<bool> > _reserveMap;
    GridBuildable _buildable;

    void	reserveSpaceNearResources();

    void	setReserve(const BWAPI::TilePosition & position, int width, int height, bool flag);

    BWAPI::TilePosition connectedWalkableTileNear(const BWAPI::TilePosition & start) const;

    bool    enemyMacroLocation(MacroLocation loc) const;
    bool	boxOverlapsBase(int x1, int y1, int x2, int y2) const;
    bool	tileBlocksAddon(const BWAPI::TilePosition & position) const;

    bool    buildableTerrain(int x1, int y1, int width, int height) const;

    bool	isFreeTile(int x, int y) const;
    bool	freeOnTop(const BWAPI::TilePosition & tile, BWAPI::UnitType buildingType) const;
    bool	freeOnRight(const BWAPI::TilePosition & tile, BWAPI::UnitType buildingType) const;
    bool	freeOnLeft(const BWAPI::TilePosition & tile, BWAPI::UnitType buildingType) const;
    bool	freeOnBottom(const BWAPI::TilePosition & tile, BWAPI::UnitType buildingType) const;
    bool	freeOnAllSides(BWAPI::Unit building) const;

    bool	canBuildHere(const BWAPI::TilePosition & position, const Building & b) const;
    bool	canBuildWithSpace(const BWAPI::TilePosition & position, const Building & b, int extraSpace) const;

    bool	groupTogether(BWAPI::UnitType type) const;

    BWAPI::TilePosition findEdgeLocation(const Building & b) const;
    BWAPI::TilePosition findPylonlessBaseLocation(const Building & b);
    BWAPI::TilePosition findGroupedLocation(const Building & b) const;
    BWAPI::TilePosition findSpecialLocation(Building & b);
    BWAPI::TilePosition findAnyLocation(const Building & b, int extraSpace);

    /* CODE ADDED */
    BWAPI::TilePosition findForgeLocation(const Building& b);
    BWAPI::TilePosition findCanonLocation(const Building& b);
    BWAPI::TilePosition findSafeLocation(const Building& b);
    BWAPI::TilePosition getNearestBuildingTile(const BWAPI::TilePosition& tile, const BWAPI::UnitType) const;
    BWAPI::TilePosition findClosestBuildTile(BWAPI::Position center, BWAPI::UnitType build, BWAPI::Position backAnchor);
    BWAPI::TilePosition findBestFFEPylon(BWAPI::TilePosition forge, BWAPI::TilePosition start, BWAPI::TilePosition backDir);
    bool isWallAdjacent(const BWAPI::TilePosition& tile, const BWAPI::UnitType& building) const;
    int getDistanceToClosestMineral(const BWAPI::TilePosition& tile) const;
    bool tileHasBuilding(int x, int y) const;
    bool wallsOnTop(const BWAPI::TilePosition& tile) const;
    bool wallsOnRight(const BWAPI::TilePosition& tile) const;
    bool wallsOnLeft(const BWAPI::TilePosition& tile) const;
    bool wallsOnBottom(const BWAPI::TilePosition& tile) const;

    struct MapBox {
        int x, y, w, h;
        BWAPI::Color c;
    };
    struct MapLine {
        int x1, y1, x2, y2;
        BWAPI::Color c;
    };
    struct MapCircle {
        int cx, cy, r;
        BWAPI::Color c;
    };

    std::vector<MapBox> boxesToDraw = {};
    std::vector<MapLine> linesToDraw = {};
    std::vector<MapCircle> circlesToDraw = {};

    int     countInRange(const BWAPI::Unitset & units, BWAPI::Position xy, int range) const;



    struct FFEPlaces {

        struct PotentialLoc {
            BWAPI::TilePosition origin;
            int w; int h;

            void draw() {
                BWAPI::Position originPos(origin);
                BWAPI::Broodwar->drawBoxMap(originPos.x, originPos.y, originPos.x + w * 32, originPos.y + h * 32, BWAPI::Colors::Grey);

                for (int dx = 0; dx <= w; dx++) {
                    for (int dy = 0; dy <= h; dy++) {
                        drawSubtile(origin + BWAPI::TilePosition(dx, dy));
                    }
                }
            }

            void drawSubtile(BWAPI::TilePosition tile) {
                BWAPI::Position p(tile);
                for (int dx = 0; dx < w; dx++) {
                    for (int dy = 0; dy < h; dy++) {
                        BWAPI::TilePosition t(origin.x + dx, origin.y + dy);

                        BWAPI::Position p(t);

                        BWAPI::Broodwar->drawBoxMap(p.x + 4, p.y + 4, p.x + 32 - 4, p.y + 32 - 4, BWAPI::Colors::Grey);
                    }
                }
            }
        };

        struct CannonBounds {
            BWAPI::TilePosition topLeft;
            BWAPI::TilePosition bottomRight;

            CannonBounds() {}

            CannonBounds(BWAPI::Position chokeA, BWAPI::Position chokeB, BWAPI::Position center) {
                // Convert to tile space
                BWAPI::TilePosition a = BWAPI::TilePosition(chokeA);
                BWAPI::TilePosition b = BWAPI::TilePosition(chokeB);
                BWAPI::TilePosition c = BWAPI::TilePosition(center);

                int minX = std::min({ a.x, b.x, c.x });
                int maxX = std::max({ a.x, b.x, c.x });
                int minY = std::min({ a.y, b.y, c.y });
                int maxY = std::max({ a.y, b.y, c.y });

                // Expand slightly toward center side
                int dx = (c.x < (a.x + b.x) / 2) ? -2 : 2;
                int dy = (c.y < (a.y + b.y) / 2) ? -2 : 2;

                minX += std::min(0, dx);
                maxX += std::max(0, dx);
                minY += std::min(0, dy);
                maxY += std::max(0, dy);

                // Clamp to avoid negative tiles
                minX = std::max(0, minX);
                minY = std::max(0, minY);

                topLeft = BWAPI::TilePosition(minX, minY);
                bottomRight = BWAPI::TilePosition(maxX, maxY);
            }

            bool contains(BWAPI::TilePosition p) const {
                return p.x >= topLeft.x &&
                    p.y >= topLeft.y &&
                    p.x <= bottomRight.x &&
                    p.y <= bottomRight.y;
            }
        };


        PotentialLoc forgePlan;
        PotentialLoc pylonPlan;

        CannonBounds cannonBounds;

    };
    
    std::unique_ptr<FFEPlaces> myNatFFEPlaces = nullptr;
    void InitializeFFE();

    bool pylonPowersForge(BWAPI::TilePosition pylon, BWAPI::TilePosition forge) {
        const int dx = forge.x - pylon.x;
        const int dy = forge.y - pylon.y;

        switch (dy) {
            case -4:
            case  3:
                return dx >= -6 && dx <= 5;

            case -3:
            case  2:
                return dx >= -7 && dx <= 6;

            case -2:
            case -1:
            case  0:
            case  1:
                return dx >= -7 && dx <= 7;

            case  4:
                return dx >= -3 && dx <= 2;

            default:
                return false;
        }
    }

public:

    BuildingPlacer();
    void initialize();

    // Return a build location near a building's desired location, with the given margin of space.
    BWAPI::TilePosition	getBuildLocationNear(Building & b, int extraSpace);

    void				reserveTiles(const BWAPI::TilePosition & position, int width, int height);
    void				freeTiles(const BWAPI::TilePosition & position, int width, int height);
    void                freeTiles(const Building & b);
    bool				isReserved(int x, int y) const;
    bool                isReserved(const BWAPI::TilePosition & tile) const { return isReserved(tile.x, tile.y); };
    void				drawReservedTiles() const;

    bool                buildingOK(const Building & b) const;
    bool                buildingOK(const Building & b, const BWAPI::TilePosition & pos) const;

    BWAPI::TilePosition getExpoLocationTile(MacroLocation loc) const;
    BWAPI::TilePosition getMacroLocationTile(MacroLocation loc) const;
    BWAPI::Position     getMacroLocationPos(MacroLocation loc) const;
    BWAPI::TilePosition	getRefineryPosition() const;

    BWAPI::TilePosition getTerrainProxyPosition(const Base * base) const;
    BWAPI::TilePosition getInBaseProxyPosition(const Base * base) const;
    BWAPI::TilePosition getProxyPosition(const Base * base) const;

    BWAPI::TilePosition getAntiBunkerSunkenPosition(const Base * base, BWAPI::Unit bunker) const;
    BWAPI::TilePosition getAntiCannonSunkenPosition(const Base * base, BWAPI::Unit cannon) const;
};
}