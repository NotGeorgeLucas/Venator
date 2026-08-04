#include "Common.h"

// Return a  UCB1 upper bound value, for an unspecified action.
// tries = the number of times the action has been tried
// total = the total number of times all actions have been tried
// Changing the constant 2.0 can alter the balance between exploration and exploitation.
// A bigger constant means more exploration.
double UCB1_bound(int tries, int total)
{
    return UCB1_bound(double(tries), double(total));
}

double UCB1_bound(double tries, double total)
{
    UAB_ASSERT(tries > 0 && total >= tries, "bad args");
    return sqrt(2.0 * log(total) / tries);
}

// Return the intersetion of two sets of units.
// It will run faster if a is the smaller set.
BWAPI::Unitset Intersection(const BWAPI::Unitset & a, const BWAPI::Unitset & b)
{
    BWAPI::Unitset result;

    for (BWAPI::Unit u : a)
    {
        if (b.contains(u))
        {
            result.insert(u);
        }
    }

    return result;
}

// Clip an integer to a range.
// Short for std::min(hi, std::max(lo, x)).
int Clip(int x, int lo, int hi)
{
    if (x <= lo)
    {
        return lo;
    }
    if (x >= hi)
    {
        return hi;
    }
    return x;
}

int GetIntFromString(const std::string & s)
{
    std::stringstream ss(s);
    int a = 0;
    ss >> a;
    return a;
}

// For example, "Zerg_Zergling" -> "Zergling"
std::string TrimRaceName(const std::string & s)
{
    if (s.substr(0, 5) == "Zerg_")
    {
        return s.substr(5, std::string::npos);
    }
    if (s.substr(0, 8) == "Protoss_")
    {
        return s.substr(8, std::string::npos);
    }
    if (s.substr(0, 7) == "Terran_")
    {
        return s.substr(7, std::string::npos);
    }

    // There is no race prefix. Return it unchanged.
    return s;
}

char RaceChar(BWAPI::Race race)
{
    if (race == BWAPI::Races::Zerg)
    {
        return 'Z';
    }
    if (race == BWAPI::Races::Protoss)
    {
        return 'P';
    }
    if (race == BWAPI::Races::Terran)
    {
        return 'T';
    }
    return 'U';
}

// Make a MacroAct string look pretty for the UI.
std::string NiceMacroActName(const std::string & s)
{
    std::string nicer = TrimRaceName(s);
    std::replace(nicer.begin(), nicer.end(), '_', ' ');

    return nicer;
}

// Safely return the name of a unit type.
// NOTE Can fail for some non-unit unit types which Steamhammer does not use.
std::string UnitTypeName(BWAPI::UnitType type)
{
    if (type == BWAPI::UnitTypes::None   ) return "None";
    if (type == BWAPI::UnitTypes::Unknown) return "Unknown";

    std::string nicer = TrimRaceName(type.getName());
    std::replace(nicer.begin(), nicer.end(), '_', ' ');

    return nicer;
}

std::string UnitTypeName(BWAPI::Unit unit)
{
    return UnitTypeName(unit->getType());
}

// Post a message to the game including the bot's name.
void GameMessage(const char * message)
{
    BWAPI::Broodwar->sendText("%c%s", white, message);
    //BWAPI::Broodwar->printf("%c%s: %c%s",
    //	BWAPI::Broodwar->self()->getTextColor(), BWAPI::Broodwar->self()->getName().c_str(),
    //	white, message);
}

// Tiles at the same box distance from a point form a hollow square box.
int TileBoxDistance(const BWAPI::TilePosition & a, const BWAPI::TilePosition & b)
{
    return std::max(abs(a.x - b.x), abs(a.y - b.y));
}

// Point b specifies a direction from point a.
// Return a position at the given distance and direction from a.
// CAUTION The result may be off the map. See DistanceAndDirection() below.
// The distance can be negative.
BWAPI::Position RawDistanceAndDirection(const BWAPI::Position & a, const BWAPI::Position & b, int distance)
{
    if (a == b)
    {
        return a;
    }

    v2 difference(b - a);
    return a + (difference.normalize() * double(distance));
}

// Point b specifies a direction from point a.
// Return a position at the given distance and direction from a, clipped to the map boundaries.
// The distance can be negative.
BWAPI::Position DistanceAndDirection(const BWAPI::Position & a, const BWAPI::Position & b, int distance)
{
    return RawDistanceAndDirection(a, b, distance).makeValid();
}

// Return the speed (pixels per frame) at which unit u is approaching the position.
// It may be positive or negative.
// This is approach speed only, ignoring transverse speed. For example, if the
// unit is moving transversely, the speed may be zero.
double ApproachSpeed(const BWAPI::Position & pos, BWAPI::Unit u)
{
    UAB_ASSERT(u && u->exists() && u->getPosition().isValid(), "bad unit");

    v2 direction = v2(BWAPI::Position(u->getPosition() - pos)).normalize();
    v2 velocity = v2(u->getVelocityX(), u->getVelocityY());
    return velocity.dot(direction);
}

BWAPI::Unit NearestOf(const BWAPI::Position & pos, const BWAPI::Unitset & set)
{
    int bestDistance = MAX_DISTANCE;
    BWAPI::Unit bestUnit = nullptr;

    for (BWAPI::Unit unit : set)
    {
        int dist = unit->getDistance(pos);
        if (dist < bestDistance)
        {
            bestDistance = dist;
            bestUnit = unit;
        }
    }

    return bestUnit;
}

BWAPI::Unit NearestOf(const BWAPI::Position & pos, const BWAPI::Unitset & set, BWAPI::UnitType type)
{
    int bestDistance = MAX_DISTANCE;
    BWAPI::Unit bestUnit = nullptr;

    for (BWAPI::Unit unit : set)
    {
        if (unit->getType() == type)
        {
            int dist = unit->getDistance(pos);
            if (dist < bestDistance)
            {
                bestDistance = dist;
                bestUnit = unit;
            }
        }
    }

    return bestUnit;
}

// Find the geometric center of a set of visible units.
// We call it (0,0) if there are no units--better check this before calling.
BWAPI::Position CenterOfUnitset(const BWAPI::Unitset units)
{
    BWAPI::Position total = BWAPI::Positions::Origin;
    int n = 0;
    for (BWAPI::Unit unit : units)
    {
        if (unit->isVisible() && unit->getPosition().isValid())
        {
            ++n;
            total += unit->getPosition();
        }
    }
    if (n > 0)
    {
        return total / n;
    }
    return total;
}

// Predict a visible unit's movement a given number of frames into the future,
// on the assumption that it keeps moving in a straight line.
// If it is predicted to go off the map, clip the prediction to a valid position on the map.
BWAPI::Position PredictMovement(BWAPI::Unit unit, int frames)
{
    UAB_ASSERT(unit && unit->getPosition().isValid(), "bad unit");

    BWAPI::Position pos(
        unit->getPosition().x + int(frames * unit->getVelocityX()),
        unit->getPosition().y + int(frames * unit->getVelocityY())
    );
    return pos.makeValid();
}


/* CODE ADDED */
// Fixed CanCatchUnit for  melee units
bool areMostlyParallel(const BWAPI::Position& a, const BWAPI::Position& b, double threshold = 0.9) {
    double dot = a.x * b.x + a.y * b.y;

    double magA = std::sqrt(a.x * a.x + a.y * a.y);
    double magB = std::sqrt(b.x * b.x + b.y * b.y);

    if (magA == 0 || magB == 0) return false; // zero vector has no direction

    double cosTheta = dot / (magA * magB);

    return std::abs(cosTheta) >= threshold;
}


// Estimate whether the chaser can catch the runaway.
// Made mostly for melee units
bool CanCatchUnit(BWAPI::Unit chaser, BWAPI::Unit runaway) {

    if (runaway->getVelocityX() == 0 && runaway->getVelocityY() == 0) return true;

    BWAPI::Position cFuture = PredictMovement(chaser, 8);
    BWAPI::Position rFuture = PredictMovement(runaway, 8);

    BWAPI::Position chaserVector = cFuture - chaser->getPosition();
    BWAPI::Position runawayVector = rFuture - runaway->getPosition();

    BWAPI::Position d = runaway->getPosition() - chaser->getPosition();

    double dist = d.getLength();
    if (dist == 0) return true;

    // normalize separation direction
    double ux = d.x / dist;
    double uy = d.y / dist;

    // projection of velocities onto separation axis
    double closingSpeed =
        (chaserVector.x - runawayVector.x) * ux +
        (chaserVector.y - runawayVector.y) * uy;

    return closingSpeed > 0;
}

// Ground height, folding the "doodad" levels into the regular levels.
// 0 - low ground, low ground doodad
// 2 - high ground, high ground doodad
// 4 - very high ground, very high ground doodad
// x and y mark a tile position.
int GroundHeight(int x, int y)
{
    return BWAPI::Broodwar->getGroundHeight(x, y) & (~0x01);
}

// Ground height, folding the "doodad" levels into the regular levels.
int GroundHeight(const BWAPI::TilePosition & tile)
{
    return BWAPI::Broodwar->getGroundHeight(tile) & (~0x01);
}

BWAPI::Position TileCenter(const BWAPI::TilePosition & tile)
{
    return BWAPI::Position(tile) + BWAPI::Position(16, 16);
}
