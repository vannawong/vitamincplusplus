
#ifndef LUALEVEL_H
#define LUALEVEL_H

#include <Box2D/Box2D.h>
#include "Render.h"
#include "LuaPlusFramework\LuaPlus.h"
#include <vector>
using namespace LuaPlus;

#include <cstdlib>
using namespace std;

class LuaLevel;
struct Settings;

typedef LuaLevel* LuaLevelCreateFcn();

#define	RAND_LIMIT	32767

/// Random number in range [-1,1]
inline float32 RandomFloat()
{
	float32 r = (float32)(std::rand() & (RAND_LIMIT));
	r /= RAND_LIMIT;
	r = 2.0f * r - 1.0f;
	return r;
}

/// Random floating point number in range [lo, hi]
inline float32 RandomFloat(float32 lo, float32 hi)
{
	float32 r = (float32)(std::rand() & (RAND_LIMIT));
	r /= RAND_LIMIT;
	r = (hi - lo) * r + lo;
	return r;
}

/// LuaLevel settings. Some can be controlled in the GUI.
struct Settings
{
	Settings() :
		viewCenter(0.0f, 20.0f),
		hz(60.0f),
		velocityIterations(8),
		positionIterations(3),
		drawShapes(1),
		drawJoints(1),
		drawAABBs(0),
		drawPairs(0),
		drawContactPoints(0),
		drawContactNormals(0),
		drawContactForces(0),
		drawFrictionForces(0),
		drawCOMs(0),
		enableWarmStarting(1),
		enableContinuous(1),
		enableSubStepping(0),
		pause(0),
		singleStep(0)
		{}

	b2Vec2 viewCenter;
	float32 hz;
	int32 velocityIterations;
	int32 positionIterations;
	int32 drawShapes;
	int32 drawJoints;
	int32 drawAABBs;
	int32 drawPairs;
	int32 drawContactPoints;
	int32 drawContactNormals;
	int32 drawContactForces;
	int32 drawFrictionForces;
	int32 drawCOMs;
	int32 enableWarmStarting;
	int32 enableContinuous;
	int32 enableSubStepping;
	int32 pause;
	int32 singleStep;
};

struct LuaLevelEntry
{
	const char *name;
	LuaLevelCreateFcn *createFcn;
};

extern LuaLevelEntry g_luaLevelEntries[];
// This is called when a joint in the world is implicitly destroyed
// because an attached body is destroyed. This gives us a chance to
// nullify the mouse joint.
class LuaLevelDestructionListener : public b2DestructionListener
{
public:
	void SayGoodbye(b2Fixture* fixture) { B2_NOT_USED(fixture); }
	void SayGoodbye(b2Joint* joint);

	LuaLevel* luaLevel;
};

const int32 k_maxContactPoints = 2048;

struct ContactPoint
{
	b2Fixture* fixtureA;
	b2Fixture* fixtureB;
	b2Vec2 normal;
	b2Vec2 position;
	b2PointState state;
};

enum GameState
{
	SPLASH,
	MENU,
		MENU_ABOUT,
		MENU_HELP,
	GAME
};

struct Texture
{
	Texture():imageWidth(0),imageHeight(0),scaledImageWidth(0),scaledImageHeight(0),id(0){}
	unsigned int imageWidth, imageHeight;
	double scaledImageWidth, scaledImageHeight;
	unsigned int id;
};

class LuaLevel : public b2ContactListener
{
public:

	LuaLevel();
	virtual ~LuaLevel();

	void SetTextLine(int32 line) { m_textLine = line; }
    void DrawTitle(int x, int y, const char *string);
	void drawGame(Settings* settings);
	virtual void Step(Settings* settings,float32 &viewZoom);
	void Keyboard(unsigned char key);
	void KeyboardUp(unsigned char key);
	void ShiftMouseDown(const b2Vec2& p);
	virtual void MouseDown(const b2Vec2& p);
	virtual void MouseUp(const b2Vec2& p);
	void MouseMove(const b2Vec2& p);
	// Let derived tests know that a joint was destroyed.
	virtual void JointDestroyed(b2Joint* joint) { B2_NOT_USED(joint); }

	// Callbacks for derived classes.
	virtual void BeginContact(b2Contact* contact) { B2_NOT_USED(contact); }
	virtual void EndContact(b2Contact* contact) { B2_NOT_USED(contact); }
	virtual void PreSolve(b2Contact* contact, const b2Manifold* oldManifold);
	virtual void PostSolve(const b2Contact* contact, const b2ContactImpulse* impulse)
	{
		B2_NOT_USED(contact);
		B2_NOT_USED(impulse);
	}
	
	void loadLevelGlobals(LuaState *pstate);
	void unloadLevelGlobals(LuaState *pstate);
	int createAnEdge(float32 x1, float32 y1, float32 x2, float32 y2);

protected:
	friend class LuaLevelDestructionListener;
	friend class BoundaryListener;
	friend class ContactListener;
	
	LuaState* luaPState;
	b2Body* m_groundBody;
	b2Body* playerBody;
	b2Fixture* playerFeet;
	b2AABB m_worldAABB;
	ContactPoint m_points[k_maxContactPoints];
	int32 m_pointCount;
	LuaLevelDestructionListener m_destructionListener;
	DebugDraw m_debugDraw;
	int32 m_textLine;
	b2World* m_world;
	b2Vec2 m_mouseWorld;
	int32 m_stepCount;
	GameState gameState;

	bool controlLeft,
		 controlRight,
		 controlJump,
		 wasMoving;
	bool isFeetTouchingBoundary, canJump, justKickedOff;
	float32 playerCanMoveUpwards;
	
	Texture aboutImage;
	Texture menuImage;
	Texture helpImage;
	Texture idleImages[3];
	int framecount;
};

#endif
