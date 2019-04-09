/*
 *  WormInputHandler.cpp
 *  OpenLieroX
 *
 *  Created by Albert Zeyer on 08.12.09.
 *  code under LGPL
 *
 */

#include "WormInputHandler.h"

#include "gusanos/net_worm.h"
#include "gusanos/objects_list.h"
#include "gusanos/gconsole.h"
#include "gusanos/encoding.h"
#include "gusanos/weapon_type.h"
#include "util/log.h"
#include "game/Game.h"
#include "CGameScript.h"
#ifndef DEDICATED_ONLY
#include "gusanos/player_input.h"
#endif
#include "gusanos/LuaCallbacks.h"
#include "gusanos/lua/bindings-game.h"
#include "gusanos/gusgame.h"

#include "gusanos/netstream.h"
#include "gusanos/network.h"
//#include "gusanos/allegro.h"
#include <list>

#include "CServer.h"
#include "CServerConnection.h"
#include "CServerNetEngine.h"

using namespace std;

Net_ClassID CWormInputHandler::classID = INVALID_CLASS_ID;
LuaReference CWormInputHandler::metaTable;

CWormInputHandler::Stats::~Stats()
{
	luaData.destroy();
}

void CWormInputHandler::gusInit(CWorm* worm)
{
	stats = boost::shared_ptr<Stats>(new Stats());
	deleteMe=(false);
	
	local=(false);
	m_node=(0);
	m_interceptor=(0);
	deleted=(false);
	
	worm->m_owner = this;
}

CWormInputHandler::~CWormInputHandler() {}

void CWormInputHandler::deleteThis()
{
	notes << "CWormInputHandler:deleteThis: " << (m_worm ? m_worm->getName() : "NOWORM") << endl;

	LUACALLBACK(playerRemoved).call()(getLuaReference())();
	
	game.onRemovePlayer(this);

	deleted = true; // We set this after the callback loop otherwise getLuaReference() will think the player is already deleted
	
	for (Grid::iterator objIter = game.objects.beginAll(); objIter; ++objIter) {
		objIter->removeRefsToPlayer(this);
	}
	
	if ( m_worm ) {
		if(m_worm->m_inputHandler == this)
			m_worm->m_inputHandler = NULL;
		m_worm = 0;
	}
	
	delete m_node; m_node = 0;
	delete m_interceptor; m_interceptor = 0;

	BaseObject::deleteThis();
}

void CWormInputHandler::addEvent(BitStream* data, CWormInputHandler::NetEvents event)
{
#ifdef COMPACT_EVENTS
	//	data->addInt(event, Encoding::bitsOf(CWormInputHandler::EVENT_COUNT - 1));
	Encoding::encode(*data, event, CWormInputHandler::EVENT_COUNT);
#else
	
	data->addInt(static_cast<int>(event),8 );
#endif
}

void CWormInputHandler::think()
{
	if(!m_worm) return;
	
	// for now, we ignore this totally if we use a lx-mod
	if(game.gameScript()->gusEngineUsed()) {
		// OLX input
		OlxInputToGusEvents();
		
		subThink();
	}
	
	if ( m_node ) {
		while ( m_node && m_node->checkEventWaiting() ) {
			eNet_Event type;
			eNet_NodeRole    remote_role;
			Net_ConnID       conn_id;
			
			BitStream *data = m_node->getNextEvent(&type, &remote_role, &conn_id);
			switch ( type ) {
				case eNet_EventUser:
					if ( data ) {
#ifdef COMPACT_EVENTS
						NetEvents event = (NetEvents)data->getInt(Encoding::bitsOf(EVENT_COUNT - 1));
#else
						
						NetEvents event = (NetEvents)data->getInt(8);
#endif
						
						switch ( event ) {
							case ACTION_START: // ACTION TART LOL TBH
							{
#ifdef COMPACT_ACTIONS
								BaseActions action = (BaseActions)data->getInt(Encoding::bitsOf(ACTION_COUNT - 1));
#else
								
								BaseActions action = (BaseActions)data->getInt(8);
#endif
								
								baseActionStart(action);
							}
								break;
							case ACTION_STOP: {
#ifdef COMPACT_ACTIONS
								BaseActions action = (BaseActions)data->getInt(Encoding::bitsOf(ACTION_COUNT - 1));
#else
								
								BaseActions action = (BaseActions)data->getInt(8);
#endif
								
								baseActionStop(action);
							}
								break;
							case SYNC: {
								stats->kills = data->getInt(32);
								stats->deaths = data->getInt(32);
								uniqueID = data->getInt(32);
							}
								break;
																								
							case SELECT_WEAPONS: {
								size_t size = Encoding::decode(*data, gusGame.options.maxWeapons+1);
								if(size > gusGame.options.maxWeapons)
									break; // Avoid horrible crashes etc.
								
								vector<WeaponType*> weaps(size,0);
								for ( size_t i = 0; i < size; ++i ) {
									weaps[i] = gusGame.weaponList[Encoding::decode(*data, gusGame.weaponList.size())];
								}
								selectWeapons(weaps);
							}
								break;
								
							case LuaEvent: {
								int index = data->getInt(8);
								if(LuaEventDef* event = network.indexToLuaEvent(Network::LuaEventGroup::CWormHumanInputHandler, index)) {
									event->call(getLuaReference(), data);
								}
							}
								break;
								
							case EVENT_COUNT:
								break; // Do nothing
						}
					}
					break;
				case eNet_EventInit: {
					sendSyncMessage( conn_id );
					
					LUACALLBACK(playerNetworkInit).call()(getLuaReference())(conn_id)();
				}
					break;
				case eNet_EventRemoved: {
					deleteThis();
				}
					break;
					
				default:
					break; // Got tired of spam that makes me miss important warnings
			}
		}
	}
		
	LUACALLBACK(playerUpdate).call()(getLuaReference())();
}

void CWormInputHandler::sendLuaEvent(LuaEventDef* event, eNet_SendMode mode, Net_U8 rules, BitStream* userdata, Net_ConnID connID)
{
	if(!m_node)
		return;
	BitStream* data = new BitStream;
	addEvent(data, LuaEvent);
	data->addInt(event->idx, 8);
	if(userdata) {
		data->addBitStream(*userdata);
	}
	if(!connID)
		m_node->sendEvent(mode, rules, data);
	else
		m_node->sendEventDirect(mode, data, connID);
}

void CWormInputHandler::addActionStart(BitStream* data, CWormInputHandler::BaseActions action)
{
	addEvent(data, CWormInputHandler::ACTION_START);
#ifdef COMPACT_ACTIONS
	data->addInt(action, Encoding::bitsOf(CWormInputHandler::ACTION_COUNT - 1));
#else
	data->addInt(static_cast<int>(action),8 );
#endif
}

void CWormInputHandler::addActionStop(BitStream* data, CWormInputHandler::BaseActions action)
{
	addEvent(data, CWormInputHandler::ACTION_STOP);
#ifdef COMPACT_ACTIONS	
	data->addInt(action, Encoding::bitsOf(CWormInputHandler::ACTION_COUNT - 1));
#else	
	data->addInt(static_cast<int>(action),8 );
#endif
}

void CWormInputHandler::selectWeapons( vector< WeaponType* > const& weaps )
{
	if ( !network.isClient() && m_worm ) {
		m_worm->setWeapons( weaps );
	}
	if ( network.isClient() && m_node ) {
		if(weaps.size() > gusGame.options.maxWeapons) {
			cerr << "ERROR: Requested a too large weapon selection" << endl;
			return;
		}
		
		BitStream *data = new BitStream;
		addEvent(data, SELECT_WEAPONS );
		
		Encoding::encode(*data, weaps.size(), gusGame.options.maxWeapons+1 );
		for( vector<WeaponType*>::const_iterator iter = weaps.begin(); iter != weaps.end(); ++iter ) {
			Encoding::encode(*data, (*iter)->getIndex(), gusGame.weaponList.size());
		}
		m_node->sendEvent(eNet_ReliableOrdered, Net_REPRULE_OWNER_2_AUTH, data);
	}
}

void CWormInputHandler::assignWorm(CWorm* worm)
{
	m_worm = worm;
	m_worm->m_owner = this;
}

bool CWormInputHandler::weOwnThis() const {
	if(m_worm) return m_worm->weOwnThis();
	return game.isServer();
}

CServerConnection* CWormInputHandler::ownerClient() const {
	if(m_worm) return m_worm->ownerClient();
	return NULL;
}

void CWormInputHandler::assignNetworkRole()
{
	if(!gusGame.isEngineNeeded() && game.isClient() && weOwnThis())
		// probably we are on an older server or so; anyway, we cannot create an authority node as client
		return;
	
	m_node = new Net_Node();
	if (!m_node) {
		allegro_message("ERROR: Unable to create player node.");
	}
	
	m_node->beginReplicationSetup();
	/*
	m_node->setInterceptID( static_cast<Net_InterceptID>(WormID) );
	m_node->addReplicationInt( (Net_S32*)&m_wormID, 32, false, Net_REPFLAG_MOSTRECENT | Net_REPFLAG_INTERCEPT, Net_REPRULE_AUTH_2_ALL , INVALID_NODE_ID);
	 */
	m_node->endReplicationSetup();
	
	m_interceptor = new BasePlayerInterceptor( this );
	m_node->setReplicationInterceptor(m_interceptor);
	
	if(weOwnThis()) {
		if(m_worm) {
			BitStream* announceData = new BitStream();
			announceData->addInt(m_worm->getID(), 8);
			m_node->setAnnounceData(announceData);
						
			if(!m_worm->getLocal()) {
				CServerConnection* cl = m_worm->getClient();
				if(cl)
					m_node->setOwner(NetConnID_conn(cl));
				else
					errors << "CWormInputHandler::assignNetworkRole: connection of worm " << m_worm->getName() << " not found" << endl;
			}
		}
		else
			errors << "CWormInputHandler::assignNetworkRole: cannot be authority node without worm" << endl;	
	}
	
	if(weOwnThis()) {
		m_node->setEventNotification(true, false); // Enables the eEvent_Init.
		if( !m_node->registerNodeDynamic(classID, network.getNetControl() ) )
			allegro_message("ERROR: Unable to register player authority node.");
	} else {
		m_node->setEventNotification(false, true); // Same but for the remove event.
		if( !m_node->registerRequestedNode( classID, network.getNetControl() ) )
			allegro_message("ERROR: Unable to register player requested node.");
	}
	
	m_node->applyForNetLevel(1);
}



void CWormInputHandler::sendSyncMessage( Net_ConnID id )
{
	BitStream *data = new BitStream;
	addEvent(data, SYNC);
	data->addInt(stats->kills, 32);
	data->addInt(stats->deaths, 32);
	data->addInt(uniqueID, 32);
	m_node->sendEventDirect(eNet_ReliableOrdered, data, id);
}

Net_NodeID CWormInputHandler::getNodeID()
{
	if (m_node)
		return m_node->getNetworkID();
	else
		return INVALID_NODE_ID;
}

Net_ConnID CWormInputHandler::getConnectionID()
{
	if(m_worm && m_worm->getNode())
		return m_worm->getNode()->getOwner();

	return INVALID_NODE_ID;
}

BasePlayerInterceptor::BasePlayerInterceptor( CWormInputHandler* parent )
{
	m_parent = parent;
}

bool BasePlayerInterceptor::inPreUpdateItem (Net_Node *_node, Net_ConnID _from, eNet_NodeRole _remote_role, Net_Replicator *_replicator)
{
	switch ( (CWormInputHandler::ReplicationItems) _replicator->getSetup()->getInterceptID() ) {
		case CWormInputHandler::WormID: {
			/*
			Net_NodeID recievedID = *static_cast<Net_U32*>(_replicator->peekData());
			
			for ( Grid::iterator iter = game.objects.beginAll(); iter; ++iter) {
				if ( CWorm* worm = dynamic_cast<CWorm*>(&*iter)) {
					if ( worm->getNodeID() == recievedID ) {
						m_parent->assignWorm(worm);
					}
				}
			}
			 */
			return true;
		}
			break;
			
		case CWormInputHandler::Other:
			break; // Do nothing?
	}
	return false;
}



void CWormInputHandler::baseActionStart ( BaseActions action )
{
	switch (action) {
		case LEFT: {
			if ( m_worm ) {
				m_worm -> actionStart(CWorm::MOVELEFT);
			}
			if ( m_node ) {
				BitStream *data = new BitStream;
				addActionStart(data, LEFT);
				m_node->sendEvent(eNet_ReliableOrdered, Net_REPRULE_AUTH_2_PROXY | Net_REPRULE_OWNER_2_AUTH, data);
			}
		}
			break;
			
		case RIGHT: {
			if ( m_worm ) {
				m_worm -> actionStart(CWorm::MOVERIGHT);
			}
			if ( m_node ) {
				BitStream *data = new BitStream;
				addActionStart(data, RIGHT);
				m_node->sendEvent(eNet_ReliableOrdered, Net_REPRULE_AUTH_2_PROXY | Net_REPRULE_OWNER_2_AUTH, data);
			}
		}
			break;
			
		case FIRE: {
			if ( m_worm ) {
				m_worm -> actionStart(CWorm::FIRE);
				if ( m_node ) {
					BitStream *data = new BitStream;
					addActionStart(data, FIRE);
					m_node->sendEvent(eNet_ReliableOrdered, Net_REPRULE_AUTH_2_PROXY | Net_REPRULE_OWNER_2_AUTH, data);
				}
			}
		}
			break;
			
		case JUMP: {
			if ( m_worm ) {
				m_worm -> actionStart(CWorm::JUMP);
			}
			if ( m_node ) {
				BitStream *data = new BitStream;
				addActionStart(data, JUMP);
				m_node->sendEvent(eNet_ReliableOrdered, Net_REPRULE_AUTH_2_PROXY | Net_REPRULE_OWNER_2_AUTH, data);
			}
		}
			break;
			
		case NINJAROPE: {
			if ( m_worm ) {
				m_worm->actionStart(CWorm::NINJAROPE);
			}
			if ( m_node ) {
				BitStream *data = new BitStream;
				addActionStart(data, NINJAROPE);
				m_node->sendEvent(eNet_ReliableOrdered, Net_REPRULE_AUTH_2_PROXY | Net_REPRULE_OWNER_2_AUTH, data);
			}
		}
			break;
			
		case DIG: {
			if ( m_worm ) {
				m_worm->dig();
			}
			if ( m_node ) {
				BitStream *data = new BitStream;
				addActionStart(data, DIG);
				m_node->sendEvent(eNet_ReliableOrdered, Net_REPRULE_AUTH_2_PROXY | Net_REPRULE_OWNER_2_AUTH, data);
			}
		}
			break;
			
		case RESPAWN: {
			if ( m_worm && !m_worm->isActive() ) {
				m_worm->respawn();
			}
			if ( m_node ) {
				BitStream *data = new BitStream;
				addActionStart(data, RESPAWN);
				m_node->sendEvent(eNet_ReliableOrdered, Net_REPRULE_AUTH_2_PROXY | Net_REPRULE_OWNER_2_AUTH, data);
				// I am sending the event to both the auth and the proxies, but I
				//cant really think of any use for the proxies knowing this, maybe
				//I should change it to only send to auth? :o
			}
		}
			break;
			
		case ACTION_COUNT:
			break; // Do nothing
	}
}



void CWormInputHandler::baseActionStop ( BaseActions action )
{
	switch (action) {
		case LEFT: {
			if ( m_worm ) {
				m_worm -> actionStop(CWorm::MOVELEFT);
			}
			if ( m_node ) {
				BitStream *data = new BitStream;
				addActionStop(data, LEFT);
				m_node->sendEvent(eNet_ReliableOrdered, Net_REPRULE_AUTH_2_PROXY | Net_REPRULE_OWNER_2_AUTH, data);
			}
		}
			break;
			
		case RIGHT: {
			if ( m_worm ) {
				m_worm -> actionStop(CWorm::MOVERIGHT);
			}
			if ( m_node ) {
				BitStream *data = new BitStream;
				addActionStop(data, RIGHT);
				m_node->sendEvent(eNet_ReliableOrdered, Net_REPRULE_AUTH_2_PROXY | Net_REPRULE_OWNER_2_AUTH, data);
			}
		}
			break;
			
		case FIRE: {
			if ( m_worm ) {
				m_worm -> actionStop(CWorm::FIRE);
			}
			if ( m_node ) {
				BitStream *data = new BitStream;
				addActionStop(data, FIRE);
				m_node->sendEvent(eNet_ReliableOrdered, Net_REPRULE_AUTH_2_PROXY | Net_REPRULE_OWNER_2_AUTH, data);
			}
		}
			break;
			
		case JUMP: {
			if ( m_worm ) {
				m_worm -> actionStop(CWorm::JUMP);
			}
			if ( m_node ) {
				BitStream *data = new BitStream;
				addActionStop(data, JUMP);
				m_node->sendEvent(eNet_ReliableOrdered, Net_REPRULE_AUTH_2_PROXY | Net_REPRULE_OWNER_2_AUTH, data);
			}
		}
			break;
			
		case NINJAROPE: {
			if ( m_worm ) {
				m_worm->actionStop(CWorm::NINJAROPE);
			}
			if ( m_node ) {
				BitStream *data = new BitStream;
				addActionStop(data, NINJAROPE);
				m_node->sendEvent(eNet_ReliableOrdered, Net_REPRULE_AUTH_2_PROXY | Net_REPRULE_OWNER_2_AUTH, data);
			}
		}
			
		case DIG:
			break; // Doesn't stop
			
		case RESPAWN:
			break; // Doesn't stop
			
		case ACTION_COUNT:
			break; // Do nothing
	}
}

void CWormInputHandler::OlxInputToGusEvents()
{
	// Notes:
	// getInput only works when the worm is alive.
	// getInput is the actual old LX handling.
	// eventStart/eventStop is the most basic event handling
	// and there might be some Lua callbacks hooked on them (called by them).
	// This will call player.actionStart/..Stop.
	// And this will call player.basicActionStart/..Stop.

	if(m_worm == NULL) return;

	worm_state_t oldS = m_worm->tState.get();
	DIR_TYPE oldMoveDir = m_worm->getMoveDirectionSide();

	{
		CGameObject::ScopedLXCompatibleSpeed wormVelScope(*m_worm);
		CGameObject::ScopedLXCompatibleSpeed ninjaVelScope(m_worm->cNinjaRope.write());
		getInput();
	}

	const worm_state_t& newS = m_worm->tState.get();
	DIR_TYPE newMoveDir = m_worm->getMoveDirectionSide();

	if(oldS.bMove && newS.bMove) {
		if(oldMoveDir == DIR_LEFT && newMoveDir == DIR_RIGHT) {
			baseActionStop(LEFT);
			baseActionStart(RIGHT);
		}
		if(oldMoveDir == DIR_RIGHT && newMoveDir == DIR_LEFT) {
			baseActionStop(RIGHT);
			baseActionStart(LEFT);
		}
	}
	if(oldS.bMove && !newS.bMove) baseActionStop((oldMoveDir == DIR_LEFT) ? LEFT : RIGHT);
	if(!oldS.bMove && newS.bMove) baseActionStart((newMoveDir == DIR_LEFT) ? LEFT : RIGHT);

	if(oldS.bJump && !newS.bJump) baseActionStop(JUMP);
	if(!oldS.bJump && newS.bJump) baseActionStart(JUMP);

	if(oldS.bShoot && !newS.bShoot) baseActionStop(FIRE);
	if(!oldS.bShoot && newS.bShoot) baseActionStart(FIRE);

	if(oldS.bCarve && !newS.bCarve) baseActionStop(DIG);
	if(!oldS.bCarve && newS.bCarve) baseActionStart(DIG);
}


void CWormInputHandler::addDeath() {
	if(m_worm) m_worm->addDeath();
	stats->deaths++;
}

// TODO: very hacky, make this in a clean way / more merging of gus&olx
static void sendWormScoreUpdate(CWorm* w) {
	for(int ii = 0; ii < MAX_CLIENTS; ii++) {
		if(cServer->getClients()[ii].getStatus() != NET_CONNECTED) continue;
		if(cServer->getClients()[ii].getNetEngine() == NULL) continue;
		cServer->getClients()[ii].getNetEngine()->SendWormScore( w );
	}
}

void CWormInputHandler::addKill() {
	if(m_worm) m_worm->addKill();
	stats->kills++;
	
	if(m_worm && game.isServer()) sendWormScoreUpdate(m_worm);
}

