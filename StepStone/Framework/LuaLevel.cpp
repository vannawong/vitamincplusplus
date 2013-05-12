#include "LuaLevel.h"
/*
* Copyright (c) 2006-2009 Erin Catto http://www.box2d.org
*
* This software is provided 'as-is', without any express or implied
* warranty.  In no event will the authors be held liable for any damages
* arising from the use of this software.
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely, subject to the following restrictions:
* 1. The origin of this software must not be misrepresented; you must not
* claim that you wrote the original software. If you use this software
* in a product, an acknowledgment in the product documentation would be
* appreciated but is not required.
* 2. Altered source versions must be plainly marked as such, and must not be
* misrepresented as being the original software.
* 3. This notice may not be removed or altered from any source distribution.
*/
#include <cstdio>
#include <iostream>
#include <cmath>
#include <string>
#include <limits>       // std::numeric_limits

using namespace std;

void LuaLevelDestructionListener::SayGoodbye(b2Joint* joint)
{
}
const static uint16 debrisBits = 1<<1;
const static uint16 boundaryBits =1<<2;
const static uint16 playerFeetBits =1<<3;
const static uint16 playerBodyBits =1<<4;
const static uint16 PLAYER_FEET_TOUCHING_BOUNDARY=playerFeetBits|boundaryBits;
const static uint16 PLAYER_FEET_TOUCHING_DEBRIS=playerFeetBits|debrisBits;
const static uint16 PLAYER_BODY_TOUCHING_DEBRIS=playerBodyBits|debrisBits;

LuaLevel::LuaLevel(Settings* settings):m_world(NULL),currentLevelLuaFile("TrainingLevel.lua"),slowdownBy(50)
{
	// Init Lua
	luaPState = LuaState::Create(true);

	vector<unsigned char> image;
	image.reserve(1024*1024*4);

	loadATexture("helpscreen.png", &helpImage, image);					uniqueTextures.push_back(helpImage.id);
	loadATexture("aboutscreen.png", &aboutImage, image);					uniqueTextures.push_back(aboutImage.id);
	loadATexture("titlescreen.png", &menuImage, image);					uniqueTextures.push_back(menuImage.id);
	loadATexture("CGs\\final.png", &winImage, image);					uniqueTextures.push_back(winImage.id);
	loadATexture("backdrops\\cloudyskies.png", &backdropImage, image);	uniqueTextures.push_back(backdropImage.id);

	//Menu Music
	loadMp3File("title\\music.mp3", &menuMusic);
	menuMusic.loop=true;
	currentMusic = &menuMusic;
	playMp3File(&menuMusic);

	luaPState->GetGlobals().SetNumber("GAME",GAME);
	luaPState->GetGlobals().SetNumber("GAME_WIN",GAME_WIN) ;
	luaPState->GetGlobals().SetNumber("GAME_INTRO",GAME_INTRO);
	luaPState->GetGlobals().SetNumber("MENU",MENU);
	luaPState->GetGlobals().SetNumber("MENU_HELP",MENU_HELP);
	luaPState->GetGlobals().SetNumber("MENU_ABOUT",MENU_ABOUT);
	luaPState->GetGlobals().SetNumber("EXIT",EXIT);
	luaPState->GetGlobals().RegisterDirect("createButton", *this, &LuaLevel::createButton);
	//menu lua
	if (luaPState->DoFile("Menu.lua"))
		std::cout << "An error occured: " << luaPState->StackTop().GetString() << std::endl;

	//cheats
	slowDown = false;
	invincibility = 0;
	uncollidable = false;

	setGameState(MENU, settings);
}

LuaLevel::~LuaLevel()
{
	// Deleting all of our textures in 1 fell swoop
	glDeleteTextures(uniqueTextures.size(), &uniqueTextures[0]);
	delete animatedIdle;
	delete animatedRun;
	delete animatedJump;

	//need to reset luastepfunction to destroy luastate
	luaStepFunction.Reset();
	// Deleting our lua state/context,
	LuaState::Destroy(luaPState);
	if (m_world)
		delete m_world;

	if (currentMusic)
		Pa_AbortStream(currentMusic->pStream);
	terminateSound();
}

void LuaLevel::PostSolve(b2Contact* contact, const b2ContactImpulse* impulse)
{
	if (invincibility)
		return;

	short collision = contact->GetFixtureA()->GetFilterData().categoryBits | contact->GetFixtureB()->GetFilterData().categoryBits;

	if (collision!=PLAYER_BODY_TOUCHING_DEBRIS && collision!=PLAYER_FEET_TOUCHING_DEBRIS)
		return;
	b2WorldManifold worldManifold;
	contact->GetWorldManifold(&worldManifold);
	b2Vec2 pos = playerBody->GetPosition();
	bool aboveCenterOfMass = true;
	for(int j = 0; j < contact->GetManifold()->pointCount; j++) {
		aboveCenterOfMass &= (worldManifold.points[j].y > pos.y);
	}
	if (!aboveCenterOfMass)
		return;

	// Should the player be killed?
	int32 count = contact->GetManifold()->pointCount;

	float32 maxImpulse = 0.0f;
	for (int32 i = 0; i < count; ++i)
	{
		maxImpulse = b2Max(maxImpulse, impulse->normalImpulses[i]);
	}

	if (maxImpulse > 40.0f)
	{
		playerBody->SetUserData((void*)true);
	}
}

void LuaLevel::init()
{
	// Init Box2D World
	b2Vec2 gravity;
	gravity.Set(0.0f, -30.0f);
	if (m_world)
		delete m_world;
	m_world = new b2World(gravity);

	//~~LEVEL LOADING~~~~~~~~~~~~~~~~~~~~~~~~
	b2BodyDef bodyDef;
	bodyDef.type = b2_staticBody;
	m_groundBody = m_world->CreateBody(&bodyDef);

	loadLevelGlobals(luaPState);

	// Open the Lua Script File
	if (luaPState->DoFile(currentLevelLuaFile.c_str()))
		std::cout << "An error occured: " << luaPState->StackTop().GetString() << std::endl;


	if (luaPState->DoFile("Settings.lua"))
		std::cout << "An error occured: " << luaPState->StackTop().GetString() << std::endl;

	unloadLevelGlobals(luaPState);

	//~~~~~~PLAYER STUFF
	//~~~~~~~~~~~~~~~~~~~~Sprites
	vector<unsigned char> image;
	image.reserve(128*128*4);
	string character(luaPState->GetGlobal("character").GetString());

	vector<Graphics::Texture> textures(4);
	vector<int> framesPerImage(4);
	loadATexture(character + "\\idle\\1.png", &textures[0], image);  framesPerImage[0]=8;  uniqueTextures.push_back(textures[0].id);
	loadATexture(character + "\\idle\\2.png", &textures[1], image);  framesPerImage[1]=30; uniqueTextures.push_back(textures[1].id);
	textures[2]=textures[0];						         framesPerImage[2]=8;
	loadATexture(character + "\\idle\\3.png", &textures[3], image);  framesPerImage[3]=30; uniqueTextures.push_back(textures[3].id);
	animatedIdle = new Graphics::AnimatedTexture(textures,4,framesPerImage);

	textures.resize(8);
	framesPerImage.resize(8);
	loadATexture(character + "\\run\\1.png", &textures[0], image); framesPerImage[0]=5; uniqueTextures.push_back(textures[0].id);
	loadATexture(character + "\\run\\2.png", &textures[1], image); framesPerImage[1]=5; uniqueTextures.push_back(textures[1].id);
	loadATexture(character + "\\run\\3.png", &textures[2], image); framesPerImage[2]=5; uniqueTextures.push_back(textures[2].id);
	loadATexture(character + "\\run\\4.png", &textures[3], image); framesPerImage[3]=7; uniqueTextures.push_back(textures[3].id);
	loadATexture(character + "\\run\\5.png", &textures[4], image); framesPerImage[4]=5; uniqueTextures.push_back(textures[4].id);
	loadATexture(character + "\\run\\6.png", &textures[5], image); framesPerImage[5]=5; uniqueTextures.push_back(textures[5].id);
	loadATexture(character + "\\run\\7.png", &textures[6], image); framesPerImage[6]=5; uniqueTextures.push_back(textures[6].id);
	loadATexture(character + "\\run\\8.png", &textures[7], image); framesPerImage[7]=7; uniqueTextures.push_back(textures[7].id);
	animatedRun = new Graphics::AnimatedTexture(textures,8,framesPerImage);

	textures.resize(5);
	framesPerImage.resize(5);
	loadATexture(character + "\\jump\\1.png", &textures[0], image); framesPerImage[0]=10; uniqueTextures.push_back(textures[0].id);
	loadATexture(character + "\\jump\\2.png", &textures[1], image); framesPerImage[1]=10; uniqueTextures.push_back(textures[1].id);
	loadATexture(character + "\\jump\\3.png", &textures[2], image); framesPerImage[2]=10; uniqueTextures.push_back(textures[2].id);
	loadATexture(character + "\\jump\\4.png", &textures[3], image); framesPerImage[3]=10; uniqueTextures.push_back(textures[3].id);
	loadATexture(character + "\\jump\\5.png", &textures[4], image); framesPerImage[4]=10; uniqueTextures.push_back(textures[4].id);
	animatedJump = new Graphics::AnimatedTexture(textures,5,framesPerImage);

	//loadATexture("CGs\\openingcg"+character+".png", &introImage, image);					uniqueTextures.push_back(introImage.id);
	if (luaPState->GetGlobal("introImageFile").IsString())
	{
		cout<<luaPState->GetGlobal("introImageFile").GetString()<<endl;
		loadATexture(luaPState->GetGlobal("introImageFile").GetString(), &introImage, image);
	}
	else introImage.id=0;
	loadMp3File(("common\\" + character + "death.mp3").c_str(),&deathSound);
	deathSound.loop = false;
	cout<<("common\\" + character + "death.mp3")<<endl;
	currentAnimatedTexture = animatedIdle;

	//~~~~~~~~~~~~~~~~~Box2D Stuff
	bodyDef.type = b2_dynamicBody;
	bodyDef.position.Set(2,2);
	bodyDef.fixedRotation=true;
	playerBody = m_world->CreateBody(&bodyDef);

	b2FixtureDef fixtureDef;
	b2PolygonShape polygonShape;
	fixtureDef.shape = &polygonShape;
	polygonShape.SetAsBox(.76f,1.28f);
	fixtureDef.friction=0;
	fixtureDef.filter.categoryBits=playerBodyBits;
	fixtureDef.density=1;
	playerBox = playerBody->CreateFixture(&fixtureDef);

	b2Vec2 center(0,-1.28f);
	polygonShape.SetAsBox(.76f,.2f, center, 0);
	fixtureDef.filter.categoryBits=playerFeetBits;
	fixtureDef.density=0;
	playerFeet = playerBody->CreateFixture(&fixtureDef);

	//~~~~~~~~~~~~~~~~~User Interface
	controlJump=false;
	controlLeft= false;
	controlRight=false;

	uint32 flags = 0;
	flags += b2Draw::e_shapeBit;
	flags += b2Draw::e_jointBit;
	m_debugDraw.SetFlags(flags);

	m_destructionListener.luaLevel = this;
	m_world->SetDestructionListener(&m_destructionListener);
	m_world->SetDebugDraw(&m_debugDraw);
	m_world->SetContactListener(this);
}

void LuaLevel::drawGame(Settings* settings, float32 timeStep)
{
	if (settings->getPause())
		if (settings->getSingleStep())
			settings->setSingleStep(0);
		else
			timeStep = 0.0f;

	if (luaStepFunction.IsFunction())
	{
		LuaFunction<void> stepFunction = luaStepFunction;
		stepFunction(timeStep);
	}

	m_world->Step(timeStep, 8, 3);
#ifdef _DEBUG
	m_world->DrawDebugData();
#endif
}

void LuaLevel::processCollisionsForGame(Settings* settings)
{
	//Check for winnning
	if (playerBody->GetPosition().y>60)
	{
		if (luaPState->GetGlobal("afterWin").IsInteger())
		{
			setGameState((GameState)luaPState->GetGlobal("afterWin").GetInteger(), settings);
		}
		else if (luaPState->GetGlobal("afterWin").IsString())
		{
			currentLevelLuaFile = luaPState->GetGlobal("afterWin").GetString();
			setGameState(GAME_INTRO, settings);
		}
		return;
	}
	// check for outofbounds
	b2Vec2 pos = playerBody->GetPosition();
	if (pos.x<0 || pos.y<0 || pos.x>viewportMaximumX)
	{
		playMp3File(&deathSound);
		setGameState(GAME_INTRO,settings);
		return;
	}

	isFeetTouchingBoundary = canJump = canKickOff = false;
	b2WorldManifold worldManifold;
	for (b2ContactEdge *ce = playerBody->GetContactList() ; ce ; ce = ce->next)
	{
		b2Contact *c = ce->contact;
		if (!c->IsTouching())
			continue;

		short collision = c->GetFixtureA()->GetFilterData().categoryBits | c->GetFixtureB()->GetFilterData().categoryBits;

		/*
		// ~~~~~~~~~~~~~~ YOU LOOSE EFFECT && slowdown effect
		if (invincibility<=0)
		{
		if (collision == PLAYER_BODY_TOUCHING_DEBRIS)
		{
		b2Body* debrisBody;
		if (c->GetFixtureA()->GetFilterData().categoryBits==debrisBits)
		{
		debrisBody = c->GetFixtureA()->GetBody();
		}
		else
		{
		debrisBody = c->GetFixtureB()->GetBody();
		}
		b2Vec2 debrisSpeed = debrisBody->GetLinearVelocity();

		// loose
		if (collision == PLAYER_BODY_TOUCHING_DEBRIS)
		{
		if (debrisSpeed.Length()>15 && debrisBody->GetPosition().y>playerBody->GetPosition().y+1.f)
		{
		setGameState(GAME_INTRO,settings);
		playMp3File(&deathSound);
		return;
		}
		}
		}
		}else invincibility-=.2f;
		if (invincibility<0)
		invincibility = 0;
		*/
		if (PLAYER_FEET_TOUCHING_BOUNDARY == collision || PLAYER_FEET_TOUCHING_DEBRIS == collision) 
		{
			isFeetTouchingBoundary = true;
			c->GetWorldManifold(&worldManifold);
			b2Vec2 pos = playerBody->GetPosition();
			bool below = true, side = true;
			for(int j = 0; j < c->GetManifold()->pointCount; j++) {
				below &= (worldManifold.points[j].y < pos.y - 1.28f);
				side &= (worldManifold.points[j].x < pos.x - .76f) | (worldManifold.points[j].x > pos.x + .76f);
			}

			if (below)
			{
				canJump = true;
				if (currentAnimatedTexture==animatedJump && justJumped==false)
					currentAnimatedTexture=animatedIdle;
			}
			else if (side) {
				canKickOff = true;
				justJumped = false;
			}
		}
	}

	if (isFeetTouchingBoundary == false) {
		justKickedOff = false;
		justJumped = false;
		if (currentAnimatedTexture!=animatedJump)
			currentAnimatedTexture=animatedJump;
	}
}

void LuaLevel::processInputForGame(Settings *settings, float32 timeStep)
{
	b2Vec2 worldCenter = playerBody->GetWorldCenter();
	b2Vec2 linearVelocity = playerBody->GetLinearVelocity();

	// JUMPING
	if (playerCanMoveUpwards>=0) playerCanMoveUpwards -= timeStep;
	if (controlJump) {
		if (canJump && justJumped==false)
		{
			playerBody->ApplyLinearImpulse(b2Vec2(0,15), worldCenter);
			playerCanMoveUpwards = .3f;
			justJumped = true;
			if (currentAnimatedTexture!=animatedJump)
				currentAnimatedTexture=animatedJump;
		}
		else if (canKickOff && justKickedOff==false)
		{
			playerBody->ApplyLinearImpulse(b2Vec2(0,20), worldCenter);
			justKickedOff = true;
		}
		else if (playerCanMoveUpwards > 0)
		{
			playerBody->ApplyLinearImpulse(b2Vec2(0,2.2f), worldCenter);
		}
	}

	// HORIZONTAL MOVEMENT/RUNNING
	float32 vx = 0;
	if (controlLeft)
		vx += -200;
	if (controlRight)
		vx += 200;

	if (vx == 0) { // if not pressing left and right
		playerFeet->SetFriction(5);
		if (currentAnimatedTexture!=animatedIdle && isFeetTouchingBoundary == true && currentAnimatedTexture!=animatedJump && justJumped==false)
			currentAnimatedTexture=animatedIdle;
		if (wasMoving) {
			for (b2ContactEdge *c = playerBody->GetContactList() ; c ; c = c->next)
			{
				c->contact->ResetFriction();
			}
			wasMoving = false;
		}
	} else {
		b2Vec2 force(vx, 0);
		if (vx > 0 && linearVelocity.x < 8) {
			playerBody->ApplyForce(force, worldCenter);
			isFacingRight = true;
			//moving right
		} else if (vx < 0 && linearVelocity.x > -8) {
			playerBody->ApplyForce(force, worldCenter);
			//moving left
			isFacingRight = false;
		}
		playerFeet->SetFriction(0);
		if (currentAnimatedTexture!=animatedRun && isFeetTouchingBoundary == true && currentAnimatedTexture!=animatedJump && justJumped==false)
			currentAnimatedTexture=animatedRun;
		if (!wasMoving) {
			for (b2ContactEdge *c = playerBody->GetContactList() ; c ; c = c->next)
			{
				c->contact->ResetFriction();
			}
			wasMoving = true;
		}
	}

	b2Vec2 viewportPosition = settings->getViewPosition();
	viewportPosition.y = max(viewportPosition.y-(viewportPosition.y-(worldCenter.y-10)) * .2f,0.0f);
	viewportPosition.x = max(0.0f,viewportPosition.x-(viewportPosition.x-(worldCenter.x-10)) * .2f);
	viewportPosition.x = min(viewportMaximumX-30.f,viewportPosition.x);//30.f is viewport width
	settings->setViewPosition(viewportPosition);
}


b2Vec2 mouse;
void LuaLevel::Step(Settings* settings)
{
	switch (gameState)
	{
	case MENU:
		glColor4ub(255, 255, 255, 255);
		drawImage(&menuImage);
		break;
	case MENU_ABOUT:
		glColor4ub(255, 255, 255, 255);
		drawImage(&aboutImage);
		break;
	case MENU_HELP:
		glColor4ub(255, 255, 255, 255);
		drawImage(&helpImage);
		break;
	case GAME_WIN:
		glColor4ub(255, 255, 255, 255);
		drawImage(&winImage);
		break;
	case GAME_INTRO:
		if (isValidTexture(introImage))
		{
			glColor4ub(255, 255, 255, 255);
			drawImage(&introImage);
		}
		break;
	case GAME:		
		float32 timeStep = settings->getHz() > 0.0f ? 1.0f / settings->getHz() : float32(0.0f);

		processCollisionsForGame(settings);
		if (gameState!=GAME) // check to see if this is still the game state
			break;
		processInputForGame(settings, timeStep);
		if (slowDown)
			timeStep/=slowdownBy;

		glEnable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glColor4f(1,1,1,1);

		// ~~~~~~~~~~~~~ background drawing
		if (Graphics::isValidTexture(backgroundImage))
			Graphics::drawImage(&backgroundImage);

		// ~~~~~~~~~~~~~ tile drawing
		if (Graphics::isValidTexture(tile1Image))
		{
			if (tile1ImageDrawList.IsTable())
			{
				int drawListLength = tile1ImageDrawList.GetN();
				for (int i = 1; i <= drawListLength-3; i+=4)
				{
					Graphics::drawImage(&tile1Image, 
						(unsigned int)tile1ImageDrawList.GetByIndex(i).GetInteger(),(unsigned int)tile1ImageDrawList.GetByIndex(i+1).GetInteger(),
						(unsigned int)tile1ImageDrawList.GetByIndex(i+2).GetInteger(),(unsigned int)tile1ImageDrawList.GetByIndex(i+3).GetInteger());
				}
			}
		}
		if (invincibility)
		{
			invincibility-=.2f;
			if (invincibility<0)
				invincibility = 0;
		}
		// ~~~~~~~~~~~~~ player drawing
		if (!invincibility || (invincibility && invincibilityEffectShow))
		{
			b2Vec2 worldCenter = playerBody->GetWorldCenter();
			glPushMatrix();
			if (uncollidable)
				glColor4f(1,1,1,.5f);
			glTranslatef(worldCenter.x,worldCenter.y-1.28f-.2f*2,0);
			const float32 scale = .025f;
			glScalef(scale*(isFacingRight?1:-1),scale,scale);
			Graphics::Texture currentTexture = currentAnimatedTexture->updateAndGetTexture();
			glTranslatef(currentTexture.imageWidth/(-2.0f),0,0);
			drawImage(&currentTexture);
			glPopMatrix();
		} else {glPushMatrix();glPopMatrix();}
		if (invincibility)
		{
			invincibilityEffectTimer-=.2f;
			if (invincibilityEffectTimer<=0)
			{
				invincibilityEffectShow = !invincibilityEffectShow;
				invincibilityEffectTimer = 2;
			}
		}
		//else glLoadIdentity();
		// ~~~~~~~~~~~~~ box2d drawing
		drawGame(settings, timeStep);
		if ((bool)playerBody->GetUserData())
		{
			playMp3File(&deathSound);
			setGameState(GAME_INTRO,settings);
		}
		break;
	}
	for (unsigned int i = 0; i<buttons.size();i++)
	{
		if (find(buttons[i].statesToShow.begin(),buttons[i].statesToShow.end(),gameState)!=buttons[i].statesToShow.end())
			if (mouse.x>buttons[i].x && mouse.x<buttons[i].x+buttons[i].standard.imageWidth &&
				mouse.y>buttons[i].y && mouse.y<buttons[i].y+buttons[i].standard.imageHeight)
				Graphics::drawImage(&buttons[i].hovering,(unsigned int)buttons[i].x,(unsigned int)buttons[i].y,buttons[i].hovering.imageWidth,buttons[i].hovering.imageHeight);
			else	
				Graphics::drawImage(&buttons[i].standard,(unsigned int)buttons[i].x,(unsigned int)buttons[i].y,buttons[i].hovering.imageWidth,buttons[i].hovering.imageHeight);
	}

#ifdef _DEBUG 
	if (settings->getPause())
	{

		m_debugDraw.DrawString(200,200,"%d",glutGet(GLUT_WINDOW_HEIGHT));
		for (b2Body *body = m_world->GetBodyList() ; body ; body = body->GetNext())
		{
			b2Vec2 pos = body->GetPosition();
			b2Vec2 vel = body->GetLinearVelocity();
			m_debugDraw.DrawString(int(pos.x*(640/30)), 480-int(pos.y*(480/25)), "%.2f, %.2f", vel.x, vel.y);

		}


		m_debugDraw.DrawString(0, 15, "key 1: Game");
		m_debugDraw.DrawString(0, 30, "key 2: Menu");
		m_debugDraw.DrawString(0, 45, "key 3: About");
		m_debugDraw.DrawString(0, 60, "key 4: Help");
		m_debugDraw.DrawString(0, 75, "isplayerfeettouchingground %s", isFeetTouchingBoundary?"true":"false");
		m_debugDraw.DrawString(0, 90, "canJump %s", canJump?"true":"false");
		m_debugDraw.DrawString(0, 105, "justJumped %s", justJumped?"true":"false");
		m_debugDraw.DrawString(0, 130, "playerCanMoveUpwards %f",playerCanMoveUpwards);
	}
#endif // _DEBUG 
}
void LuaLevel::setGameState(GameState state, Settings* settings)
{
	if (gameState==GAME && state!=GAME)
	{
		delete m_world;
		if (tile1Image.id)
			glDeleteTextures(1,&tile1Image.id);
		if (tile2Image.id)
			glDeleteTextures(1,&tile2Image.id);
		if (backgroundImage.id)
			glDeleteTextures(1,&tile2Image.id);
		if (currentMusic==&gameMusic)
		{
			Pa_AbortStream(currentMusic->pStream);
			currentMusic = NULL;
		}
		luaStepFunction;
		gameMusic.loaded.resize(0);
		m_world = NULL;
	}
	gameState = state;
	if (gameState==GAME)
	{
		settings->setViewSize(30);
		settings->widthIsConstant = true;
		settings->setViewPosition(b2Vec2(0,0));
		glClearColor(0,0,0,1);
		if (currentMusic != &gameMusic)
		{
			if (currentMusic)
				Pa_AbortStream(currentMusic->pStream);
			if (gameMusic.loaded.size()!=0)
			{
				gameMusic.pos = 0;
				currentMusic = &gameMusic;
				playMp3File(&gameMusic);
			}
		}
	}
	else
	{
		if (gameState==GAME_INTRO)
			init();

		glClearColor(97/255.f,117/255.f,113/255.f,1);
		glEnable(GL_TEXTURE_2D);
		settings->setViewPosition(b2Vec2(0,0));
		settings->widthIsConstant = false;
		settings->setViewSize(1024);

		if (state!=GAME_WIN || state!= GAME_INTRO)
		{
			if (currentMusic != &menuMusic)
			{
				if (currentMusic)
					Pa_AbortStream(currentMusic->pStream);

				menuMusic.pos = 0;
				currentMusic = &menuMusic;
				playMp3File(&menuMusic);
			}
		}
		if (state==GAME_WIN || state== GAME_INTRO)
		{
			if (currentMusic)
			{
				Pa_AbortStream(currentMusic->pStream);
				currentMusic = NULL;
			}
		}

		if (state==MENU)
		{
			currentLevelLuaFile = "TrainingLevel.lua";
		}
	}
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~	INPUT HANDLING	~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void LuaLevel::Keyboard(unsigned char key, Settings* settings)
{
	if (gameState==GAME_INTRO)
	{
		setGameState(GAME, settings);
	}
	else if (gameState==GAME_WIN && key==27)
		setGameState(MENU, settings);
	else
		switch (key)
	{
		case 27: //ESCAPE KEY
			if (gameState==MENU)
				glutLeaveMainLoop();
			setGameState(MENU, settings);
			break;
		case 'r':
			playMp3File(&deathSound);
			setGameState(GAME_INTRO,settings);
			break;
		default:
			if (key==controlKeyLeft)
				controlLeft = true;
			else if (key==controlKeyRight)
				controlRight = true;
			else if (key==controlKeyJump)
				controlJump= true;
			else if (key==controlKeySlowDown)
				slowDown = !slowDown;
			else if (key==controlKeyInvincibility)
				if (invincibility>0) 
					invincibility = 0;
				else
					invincibility = numeric_limits<float>::infinity();
			else if (key==controlKeyUncollidable)
			{
				b2Filter feetFilter(playerFeet->GetFilterData()), bodyFilter(playerBox->GetFilterData());
				if (uncollidable = !uncollidable)
				{
					//uncollidable
					feetFilter.maskBits = 0;
					bodyFilter.maskBits = 0;
				}
				else
				{
					//collidable
					feetFilter.maskBits = -1;
					bodyFilter.maskBits = -1;
				}
				playerFeet->SetFilterData(feetFilter);
				playerBox->SetFilterData(bodyFilter);
			}
			break;
	}
}

void LuaLevel::KeyboardUp(unsigned char key)
{
	if (key==controlKeyLeft)
		controlLeft = false;
	else if (key==controlKeyRight)
		controlRight = false;
	else if (key==controlKeyJump)
		controlJump= false;
}

void LuaLevel::MouseDown(const b2Vec2& p, Settings *settings)
{
	mouse = p;

	for (unsigned int i = 0; i<buttons.size();i++)
	{
		if (find(buttons[i].statesToShow.begin(),buttons[i].statesToShow.end(),gameState)!=buttons[i].statesToShow.end() &&
			mouse.x>buttons[i].x && mouse.x<buttons[i].x+buttons[i].standard.imageWidth &&
			mouse.y>buttons[i].y && mouse.y<buttons[i].y+buttons[i].standard.imageHeight)
		{
			if (buttons[i].state!=EXIT)
				setGameState((GameState)buttons[i].state, settings);
			else glutLeaveMainLoop();
		}
	}
}

void LuaLevel::ShiftMouseDown(const b2Vec2& p)
{
	mouse = p;
}

void LuaLevel::MouseUp(const b2Vec2& p)
{
	mouse = p;
}

void LuaLevel::MouseMove(const b2Vec2& p)
{
	mouse = p;
}


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Lua Specific methods    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


inline void checkAndSetChar(char &aChar, LuaObject luaObject)
{
	if (luaObject.IsString())
		aChar = luaObject.GetString()[0];
}

inline void checkAndSetFloat(float &aFloat, LuaObject luaObject)
{
	if (luaObject.IsNumber())
		aFloat = (float)luaObject.GetNumber();
}

void LuaLevel::loadLevelGlobals(LuaState *pstate)
{
	//Box2DFactory/Level Loading~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	LuaObject globals = pstate->GetGlobals();

	LuaObject metaTableObj = globals.CreateTable("box2DFactoryMetaTable");
	metaTableObj.SetObject("__index", metaTableObj);
	metaTableObj.RegisterObjectDirect("createEdge", (LuaLevel *)nullptr, &LuaLevel::createAnEdge);
	metaTableObj.RegisterObjectDirect("createFrictionlessEdge", (LuaLevel *)nullptr, &LuaLevel::createFrictionlessEdge);
	metaTableObj.RegisterObjectDirect("createBox", (LuaLevel *)nullptr, &LuaLevel::createBox);
	metaTableObj.RegisterObjectDirect("createDebris", (LuaLevel *)nullptr, &LuaLevel::createDebris);

	LuaObject box2DFactoryObject = pstate->BoxPointer(this);
	box2DFactoryObject.SetMetaTable(metaTableObj);
	pstate->GetGlobals().SetObject("box2DFactory", box2DFactoryObject);

	// Controls ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	globals.SetString("controlKeyJump"," ", 1);
	globals.SetString("controlKeyRight","d",1);
	globals.SetString("controlKeyLeft","a",1);
	globals.SetString("controlSlowDown","\0",1);
	globals.SetString("controlKeyInvincibility","s",1);
	globals.SetString("controlKeyUncollidable","e",1);
	globals.SetString("character","Alex",4);
	globals.SetNumber("viewportMaximumX",30);


	//Level specific stuff
	globals.SetNil("music");
	globals.SetNil("musicLoop");

	globals.SetNil("tile1ImageFile");
	globals.SetNil("tile1ImageWidth");
	globals.SetNil("tile1ImageHeight");
	globals.SetNil("tile1ImageDrawList");

	globals.SetNil("tile2ImageFile");
	globals.SetNil("tile2ImageWidth");
	globals.SetNil("tile2ImageHeight");
	globals.SetNil("tile2ImageDrawList");

	globals.SetNil("backgroundImageFile");
	globals.SetNil("step");

	globals.SetNil("afterWin");
}

void LuaLevel::unloadLevelGlobals(LuaState *pstate)
{
	//controls
	checkAndSetChar(controlKeyLeft, pstate->GetGlobal("controlKeyLeft"));
	checkAndSetChar(controlKeyRight, pstate->GetGlobal("controlKeyRight"));
	checkAndSetChar(controlKeyJump, pstate->GetGlobal("controlKeyJump"));
	checkAndSetChar(controlKeySlowDown, pstate->GetGlobal("controlKeySlowDown"));
	checkAndSetChar(controlKeyInvincibility, pstate->GetGlobal("controlKeyInvincibility"));
	checkAndSetChar(controlKeyUncollidable, pstate->GetGlobal("controlKeyUncollidable"));

	//level specific stuffs
	//music
	if (pstate->GetGlobal("music").IsString())
	{
		loadMp3File(pstate->GetGlobal("music").GetString(),&gameMusic);
	}
	if (pstate->GetGlobal("musicLoop").IsBoolean())
	{
		gameMusic.loop = pstate->GetGlobal("musicLoop").GetBoolean();
	}
	if (pstate->GetGlobal("viewportMaximumX").IsNumber())
	{
		viewportMaximumX = (float)pstate->GetGlobal("viewportMaximumX").GetNumber();
	}


	//Tiles
	if (pstate->GetGlobal("tile1ImageFile").IsString())
	{
		Graphics::loadATexture(pstate->GetGlobal("tile1ImageFile").GetString(),&tile1Image);
		if (pstate->GetGlobal("tile1ImageWidth").IsNumber())
			tile1Image.imageWidth = (unsigned int)pstate->GetGlobal("tile1ImageWidth").GetNumber();
		if (pstate->GetGlobal("tile1ImageHeight").IsNumber())
			tile1Image.imageHeight = (unsigned int)pstate->GetGlobal("tile1ImageHeight").GetNumber();

		if (pstate  ->GetGlobal("tile1ImageDrawList").IsTable())
		{
			tile1ImageDrawList = pstate->GetGlobal("tile1ImageDrawList");
			if (tile1ImageDrawList.GetN()%4)
				cout<<"Drawing list is not a mutliple of 4"<<endl;
		}
	}
	if (pstate->GetGlobal("tile2ImageFile").IsString())
	{
		Graphics::loadATexture(pstate->GetGlobal("tile2ImageFile").GetString(),&tile2Image);
		if (pstate->GetGlobal("tile2ImageWidth").IsNumber())
			tile2Image.imageWidth = (unsigned int)pstate->GetGlobal("tile2ImageWidth").GetNumber();
		if (pstate->GetGlobal("tile2ImageHeight").IsNumber())
			tile2Image.imageHeight = (unsigned int)pstate->GetGlobal("tile2ImageHeight").GetNumber();

		if (pstate->GetGlobal("tile2ImageDrawList").IsTable())
		{
			tile2ImageDrawList = pstate->GetGlobal("tile2ImageDrawList");
			if (tile2ImageDrawList.GetN()%4)
				cout<<"Drawing list is not a mutliple of 4"<<endl;
		}
	}
	if (pstate->GetGlobal("backgroundImageFile").IsString())
	{
		Graphics::loadATexture(pstate->GetGlobal("backgroundImageFile").GetString(),&backgroundImage);
		if (pstate->GetGlobal("backgroundImageWidth").IsNumber())
			backgroundImage.imageWidth = (unsigned int)pstate->GetGlobal("backgroundImageWidth").GetNumber();
		if (pstate->GetGlobal("backgroundImageHeight").IsNumber())
			backgroundImage.imageHeight = (unsigned int)pstate->GetGlobal("backgroundImageHeight").GetNumber();
	}

	//functions
	if (pstate->GetGlobal("step").IsFunction())
	{
		luaStepFunction = pstate->GetGlobal("step");
	}
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Lua hooked methods    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

int LuaLevel::createAnEdge( float32 x1, float32 y1, float32 x2, float32 y2 )
{
	b2FixtureDef fixtureDef;
	b2EdgeShape edgeShape;


	fixtureDef.filter.categoryBits=boundaryBits;
	fixtureDef.shape = &edgeShape;

	edgeShape.Set(b2Vec2(x1,y1),b2Vec2(x2,y2));

	m_groundBody->CreateFixture(&fixtureDef);
	return 0;
}

int LuaLevel::createFrictionlessEdge(float32 x1, float32 y1, float32 x2, float32 y2)
{
	b2FixtureDef fixtureDef;
	b2EdgeShape edgeShape;

	fixtureDef.friction = 0;
	fixtureDef.filter.categoryBits=boundaryBits;
	fixtureDef.shape = &edgeShape;

	edgeShape.Set(b2Vec2(x1,y1),b2Vec2(x2,y2));

	m_groundBody->CreateFixture(&fixtureDef);
	return 0;
}

int LuaLevel::createBox( float32 x, float32 y, float32 w, float32 h)
{
	b2FixtureDef fixtureDef;
	b2PolygonShape polygonShape;


	fixtureDef.filter.categoryBits=boundaryBits;
	fixtureDef.shape = &polygonShape;

	polygonShape.SetAsBox(w/2,h/2,b2Vec2(x+w/2,y+h/2),0);

	m_groundBody->CreateFixture(&fixtureDef);
	return 0;
}

int LuaLevel::createDebris( float32 x, float32 y,  float32 w, float32 h)
{
	b2BodyDef bodyDef;
	b2FixtureDef fixtureDef;
	b2PolygonShape polygonShape;


	float32 r = ((float) rand() / (RAND_MAX));
	bodyDef.angle = r * 360 * 3.14f / 180;
	bodyDef.position.x = x;
	bodyDef.position.y= playerBody->GetPosition().y+30;
	bodyDef.type = b2_dynamicBody;
	bodyDef.fixedRotation = false;
	b2Body* body = m_world->CreateBody(&bodyDef);

	fixtureDef.filter.categoryBits = debrisBits;
	fixtureDef.density = 1;
	fixtureDef.friction = .4f;
	fixtureDef.shape = &polygonShape;

	polygonShape.SetAsBox(w,h);

	body->CreateFixture(&fixtureDef);

	return 0;
}

int LuaLevel::createButton(float x, float y, const char* file1, const char* file2, int state, LuaStackObject statesToShow)
{
	Graphics::Texture hovering,standard;
	Graphics::loadATexture(file1, &standard);
	Graphics::loadATexture(file2, &hovering);
	uniqueTextures.push_back(hovering.id);
	uniqueTextures.push_back(standard.id);
	vector<GameState> statesToShowVector;
	if (statesToShow.IsTable())
	{
		int size = statesToShow.GetCount();
		for (int i = 1; i <= size; i++)
			statesToShowVector.push_back((GameState)statesToShow.GetByIndex(i).GetInteger());
	}
	buttons.push_back(Button(x,y,hovering,standard, state, statesToShowVector));
	return 0;
}