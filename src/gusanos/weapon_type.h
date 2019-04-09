#ifndef WEAPON_TYPE_H
#define WEAPON_TYPE_H

#include "gfx.h"
#include "resource_base.h"
#include "StringUtils.h"
#ifndef DEDICATED_ONLY
#include "blitters/context.h"
#endif

#include <string>
#include <vector>
#include <boost/cstdint.hpp>

#ifndef DEDICATED_ONLY
class SpriteSet;
#endif
struct GameEvent;
struct TimerEvent;

class WeaponType : public ResourceBase
{
public:
	static LuaReference metaTable;
	virtual LuaReference getMetaTable() const { return metaTable; }

	WeaponType();
	~WeaponType();
	
	bool load(const std::string &filename);
	
	virtual void finalize();

	int ammo;
	int reloadTime;
	
	bool syncHax;
	bool syncReload;
	
	uint32_t laserSightColour;
	int laserSightRange;
	float laserSightIntensity;
	int laserSightAlpha;
	uint32_t crc;
	
#ifndef DEDICATED_ONLY
	BlitterContext::Type laserSightBlender;
	SpriteSet *firecone;
	SpriteSet *skin;
#endif
	std::string name;
	std::string fileName;

	std::vector< TimerEvent* > timer;
	std::vector< TimerEvent* > activeTimer;
	std::vector< TimerEvent* > shootTimer;

	GameEvent *primaryShoot;
	GameEvent *primaryPressed;
	GameEvent *primaryReleased;
	GameEvent *outOfAmmo;
	GameEvent *reloadEnd;
};

struct WeaponOrder
{
	bool operator () ( WeaponType* weap1, WeaponType* weap2)
	{
		if ( stringcasecmp(GetBaseFilename(weap1->fileName), GetBaseFilename(weap2->fileName)) < 0 )
			return true;
		return false;
	}
};

#endif // _WEAPON_TYPE_H_
