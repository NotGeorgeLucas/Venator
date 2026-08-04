#include "MicroTransports.h"

#include "The.h"
#include "UnitUtil.h"
#include "StrategyManager.h"

using namespace UAlbertaBot;

/* CODE ADDED */
// Reaver Dropping

// Distance between evenly-spaced waypoints, in tiles.
// Not all are evenly spaced.
const int WaypointSpacing = 5;

MicroTransports::MicroTransports()
    : _transportShip(nullptr)
    , _nextWaypointIndex(-1)
    , _lastWaypointIndex(-1)
    , _direction(0)
    , _target(BWAPI::Positions::Invalid)
{
}

bool MicroTransports::isDropMode() {
    if (_cachedDropMode < 0) {
        if (StrategyManager::Instance().dropIsPlanned()) {
            _cachedDropMode = 1;
        }
        else {
            _cachedDropMode = 0;
        }
    }

    return _cachedDropMode > 0;
}

// No micro to execute here. Does nothing, never called.
void MicroTransports::executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster)
{
}

void MicroTransports::calculateWaypoints()
{
    // Tile coordinates.
    int minX = 0;
    int minY = 0;
    int maxX = BWAPI::Broodwar->mapWidth() - 1;
    int maxY = BWAPI::Broodwar->mapHeight() - 1;

    // Add vertices down the left edge.
    for (int y = minY; y <= maxY; y += WaypointSpacing)
    {
        _waypoints.push_back(TileCenter(BWAPI::TilePosition(minX, y)));
    }
    // Add vertices across the bottom.
    for (int x = minX; x <= maxX; x += WaypointSpacing)
    {
        _waypoints.push_back(TileCenter(BWAPI::TilePosition(x, maxY)));
    }
    // Add vertices up the right edge.
    for (int y = maxY; y >= minY; y -= WaypointSpacing)
    {
        _waypoints.push_back(TileCenter(BWAPI::TilePosition(maxX, y)));
    }
    // Add vertices across the top back to the origin.
    for (int x = maxX; x >= minX; x -= WaypointSpacing)
    {
        _waypoints.push_back(TileCenter(BWAPI::TilePosition(x, minY)));
    }
}

// Turn an integer (possibly negative) into a valid waypoint index.
// The waypoints form a loop. so moving to the next or previous one is always possible.
// This calculation is also used in finding the shortest path around the map. Then
// i may be as small as -_waypoints.size() + 1.
int MicroTransports::waypointIndex(int i)
{
    UAB_ASSERT(_waypoints.size(), "no waypoints");
    const int m = int(_waypoints.size());
    return ((i % m) + m) % m;
}

// The index can be any integer. It gets mapped to a correct index first.
const BWAPI::Position & MicroTransports::waypoint(int i)
{
    return _waypoints[waypointIndex(i)];
}

void MicroTransports::drawTransportInformation()
{
    if (!Config::Debug::DrawUnitTargets)
    {
        return;
    }

    for (size_t i = 0; i < _waypoints.size(); ++i)
    {
        BWAPI::Broodwar->drawCircleMap(_waypoints[i], 4, BWAPI::Colors::Green, false);
        BWAPI::Broodwar->drawTextMap(_waypoints[i] + BWAPI::Position(-4, 4), "%d", i);
    }
    BWAPI::Broodwar->drawCircleMap(waypoint(_lastWaypointIndex), 5, BWAPI::Colors::Red, false);
    BWAPI::Broodwar->drawCircleMap(waypoint(_lastWaypointIndex), 6, BWAPI::Colors::Red, false);
    if (_target.isValid())
    {
        BWAPI::Broodwar->drawCircleMap(_target, 8, BWAPI::Colors::Purple, true);
        BWAPI::Broodwar->drawCircleMap(_target, order->getRadius(), BWAPI::Colors::Purple, false);
    }
}

void MicroTransports::update()
{
    // If we haven't found our transport, or it went away, look again.
    // Only supports having 1 transport unit.
    if (!UnitUtil::IsValidUnit(_transportShip))
    {
        if (getUnits().empty())
        {
            _transportShip = nullptr;
        }
        else
        {
            _transportShip = *(getUnits().begin());
        }
    }

    // If we still have no transport, or it's still gone, there is nothing to do.
    if (!UnitUtil::IsValidUnit(_transportShip))
    {
        _transportShip = nullptr;
        return;
    }



    if (isDropMode()) {

        // If we're not full yet, wait.
        if (_transportShip->getSpaceRemaining() > 0) {
            return;
        }

        // All clear. Go do stuff.
        maybeUnloadTroops();
        moveTransport();
    
        drawTransportInformation();

    }
    else {


        if (findAndJoinSquad(_transportShip)) {
            maybePickupCarryTarget(_transportShip);
            moveCarryTarget(_transportShip);
            maybeDropCarryTarget(_transportShip);
        }

    }
}




bool MicroTransports::findAndJoinSquad(BWAPI::Unit transport) {

    if (transport->getLoadedUnits().size() > 0) {
        return true;
    }

    const std::vector<UnitCluster> allyGroups = the.ops.getFriendlyClusters();

    if (allyGroups.size() < 1) { return false; }

    UnitCluster bestCluster = allyGroups[0];
    int bestValue = INT_MIN;

    for (UnitCluster cl : allyGroups) {

        int groupScore = 0;

        for (const auto & unit : cl.units) {
            BWAPI::UnitType type = unit->getType();

            if (type == BWAPI::UnitTypes::Protoss_Reaver) {
                groupScore += 500000;
            } else if (type == BWAPI::UnitTypes::Protoss_Dragoon) {
                groupScore += 10000;
            }
            else if(transport->canLoad(unit)) {
                groupScore += 3 * type.gasPrice() + type.mineralPrice();;
            }
        }

        if (groupScore > bestValue) {
            bestValue = groupScore;
            bestCluster = cl;
        }
    }

    if (transport->getPosition().getApproxDistance(bestCluster.center) >= bestCluster.radius) {
        the.micro.MoveNear(transport, bestCluster.center);
        return false;
    }
    else {

        BWAPI::Unit bestCarryUnit;
        int bestValue = INT_MIN;

        for (BWAPI::Unit u : bestCluster.units) {

            BWAPI::UnitType type = u->getType();
            int value = 0;

            if (type == BWAPI::UnitTypes::Protoss_Reaver) {
                bestValue += 500000;
            }
            else if (type == BWAPI::UnitTypes::Protoss_Dragoon) {
                bestValue += 10000;
            }
            else if (transport->canLoad(u)) {
                bestValue += 3 * type.gasPrice() + type.mineralPrice();
            }

            if (value > bestValue) {
                bestValue = value;
                bestCarryUnit = u;
            }

        }

        if (bestCarryUnit) {
            _currentCarryTargets.emplace(transport->getID(), bestCarryUnit);
        }

        return true;
    }
}

void MicroTransports::maybePickupCarryTarget(BWAPI::Unit transport) {
    auto it = _currentCarryTargets.find(transport->getID());
    if (it == _currentCarryTargets.end()) {
        return;
    }

    BWAPI::Unit target = it->second;
    if (!UnitUtil::IsValidUnit(target) || target->isLoaded()) {
        return;
    }

    if (transport->getSpaceRemaining() < target->getType().spaceRequired()) {
        return;
    }

    // Only try if the shuttle can actually load it.
    if (transport->canLoad(target)) {
        the.micro.Load(transport, target);
    }
}

void MicroTransports::moveCarryTarget(BWAPI::Unit transport) {

    auto it = _currentCarryTargets.find(transport->getID());
    if (it == _currentCarryTargets.end()) {
        return;
    }

    BWAPI::Unit target = it->second;
    if (!UnitUtil::IsValidUnit(target)) {
        return;
    }

    // If target is not loaded, move near it to pick it up.
    if (!target->isLoaded()) {
        the.micro.Move(transport, target->getPosition());
        return;
    }

    // If target is loaded, move toward the current destination / squad anchor.
    if (_target.isValid()) {
        the.micro.Move(transport, _target);
    }

}

void MicroTransports::maybeDropCarryTarget(BWAPI::Unit transport) {

    auto it = _currentCarryTargets.find(transport->getID());
    if (it == _currentCarryTargets.end()) {
        return;
    }

    BWAPI::Unit target = it->second;
    if (!UnitUtil::IsValidUnit(target) || !target->isLoaded()) {
        return;
    }

    const int distToTarget = _target.isValid() ? transport->getDistance(_target) : 999999;
    const int transportHP = transport->getHitPoints() + transport->getShields();

    bool shouldDrop = (distToTarget < 300) || (transportHP < 50); 
    if (!shouldDrop) {
        return;
    }

    if (transport->canUnloadAtPosition(transport->getPosition())) {
        BWAPI::UnitCommand cmd = transport->getLastCommand();
        if (cmd.getType() != BWAPI::UnitCommandTypes::Unload_All && cmd.getType() != BWAPI::UnitCommandTypes::Unload_All_Position) {
            the.micro.UnloadAt(transport, transport->getPosition());
        }
    }

}


// Called when the transport exists and is not full.
void MicroTransports::loadTroops()
{
    // If we're still busy loading the previous unit, wait.
    if (_transportShip->getLastCommand().getType() == BWAPI::UnitCommandTypes::Load)
    {
        return;
    }

    for (const BWAPI::Unit unit : getUnits())
    {
        if (unit != _transportShip && !unit->isLoaded())
        {
            the.micro.Load(_transportShip, unit);
            return;
        }
    }
}

// Only called when the transport exists and is loaded.
void MicroTransports::maybeUnloadTroops()
{
    // Unload if we're close to the destination, or if we're scary low on hit points.
    // It's possible that we'll land on a cliff and the units will be stuck there.
    const int transportHP = _transportShip->getHitPoints() + _transportShip->getShields();
    
    if ((transportHP < 50 || _target.isValid() && _transportShip->getDistance(_target) < 300) &&
        _transportShip->canUnloadAtPosition(_transportShip->getPosition()))
    {
        // get the unit's current command
        BWAPI::UnitCommand currentCommand(_transportShip->getLastCommand());

        // Tf we've already ordered unloading, wait.
        if (currentCommand.getType() == BWAPI::UnitCommandTypes::Unload_All || currentCommand.getType() == BWAPI::UnitCommandTypes::Unload_All_Position)
        {
            return;
        }

        the.micro.UnloadAt(_transportShip, _transportShip->getPosition());
    }	
}

// Called when the transport exists and is loaded.
void MicroTransports::moveTransport()
{
    // If we're busy unloading, wait.
    BWAPI::UnitCommand currentCommand(_transportShip->getLastCommand());
    if ((currentCommand.getType() == BWAPI::UnitCommandTypes::Unload_All || currentCommand.getType() == BWAPI::UnitCommandTypes::Unload_All_Position) &&
        _transportShip->getLoadedUnits().size() > 0)
    {
        return;
    }

    followPerimeter();
}

// Decide which direction to go, then follow the perimeterto the destination.
// Called only when the transport exists and is loaded.
void MicroTransports::followPerimeter()
{
    // We must have a _transportShip before calling this.
    UAB_ASSERT(hasTransportShip(), "no transport");

    // Place a loop of points around the edge of the map, to use as waypoints.
    if (_waypoints.empty())
    {
        calculateWaypoints();
    }

    // To follow the waypoints around the edge of the map, we need these things:
    // The initial waypoint index, the final waypoint index near the target,
    // the direction to follow (+1 or -1), and the _target.
    // direction == 0 means we haven't decided which direction to go around,
    // and none of them is set yet.
    if (_direction == 0)
    {
        // Set this so we don't have to deal with the order changing behind our backs.
        _target = order->getPosition();

        // Find the start and end waypoints by brute force.
        int startDistance = 999999;
        double endDistance = 999999.9;
        for (size_t i = 0; i < _waypoints.size(); ++i)
        {
            const BWAPI::Position & waypoint = _waypoints[i];
            if (_transportShip->getDistance(waypoint) < startDistance)
            {
                startDistance = _transportShip->getDistance(waypoint);
                _nextWaypointIndex = i;
            }
            if (_target.getDistance(waypoint) < endDistance)
            {
                endDistance = _target.getDistance(waypoint);
                _lastWaypointIndex = i;
            }
        }

        // Decide which direction around the map is shorter.
        int counterclockwise = waypointIndex(_lastWaypointIndex - _nextWaypointIndex);
        int clockwise = waypointIndex(_nextWaypointIndex - _lastWaypointIndex);
        _direction = (counterclockwise <= clockwise) ? 1 : -1;
    }

    // Everything is set. Do the movement.

    // If we're near the destination, go straight there.
    if (_transportShip->getDistance(waypoint(_lastWaypointIndex)) < 2 * 32 * WaypointSpacing)
    {
        // The target might be far from the edge of the map, although
        // our path around the edge of the map makes sense only if it is close.
        the.micro.Move(_transportShip, _target);
    }
    else
    {
        // If the second waypoint ahead is close enough (1.5 waypoint distances), make it the next waypoint.
        if (_transportShip->getDistance(waypoint(_nextWaypointIndex + _direction)) < 48 * WaypointSpacing)
        {
            _nextWaypointIndex = waypointIndex(_nextWaypointIndex + _direction);
        }

        // Aim for the second waypoint ahead.
        const BWAPI::Position & destination = waypoint(_nextWaypointIndex + _direction);

        if (Config::Debug::DrawUnitTargets)
        {
            BWAPI::Broodwar->drawCircleMap(destination, 5, BWAPI::Colors::Yellow, true);
        }

        the.micro.Move(_transportShip, destination);
    }
}

bool MicroTransports::hasTransportShip() const
{
    return UnitUtil::IsValidUnit(_transportShip);
}
