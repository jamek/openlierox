#include "server.h"
#include "gconsole.h"
#include "gusgame.h"
#include "net_worm.h"
#include "CWorm.h"
#include "game/WormInputHandler.h"
#include "part_type.h"
#include "player_options.h"
#include "network.h"
#include "util/math_func.h"
#include "util/macros.h"
#include "util/log.h"
#include "encoding.h"
#include "CMap.h"
#include "game/Game.h"

#include "netstream.h"
#include "gusanos/allegro.h"
#include <list>


Server::Server()
		: Net_Control(true),
		m_preShutdown(false),
		socketsInited(false)
{
	Net_setControlID(0);
	Net_setDebugName("Net_CLI");
	console.addLogMsg("GUSANOS SERVER UP");
}

Server::~Server()
{}

void Server::Net_cbDataReceived( Net_ConnID  _id, Net_BitStream &_data)
{
	Network::NetEvents event = (Network::NetEvents) _data.getInt(8);
	switch( event ) {
			case Network::PLAYER_REQUEST: {
				int wormid = _data.getInt(8);
				unsigned int uniqueID = 0;

				CWorm* worm = (wormid >= 0 && wormid < MAX_WORMS) ? &cClient->getRemoteWorms()[wormid] : NULL;
				worm->setOwnerId(_id);
				CWormInputHandler* player = gusGame.addPlayer ( GusGame::PROXY );

				do {
					uniqueID = rndgen();
				} while(!uniqueID);

				player->getOptions()->uniqueID = uniqueID;
				savedScores[uniqueID] = player->stats;

				console.addLogMsg( "* " + worm->getName() + " HAS JOINED THE GAME");
				player->assignNetworkRole(true);
				player->setOwnerId(_id);
				player->assignWorm(worm);
			}
			break;
			case Network::RConMsg: {
				char const* passwordSent = _data.getStringStatic();
				if ( !gusGame.options.rConPassword.empty() && gusGame.options.rConPassword == passwordSent ) {
					//console.addQueueCommand(_data.getStringStatic());
					console.parseLine(_data.getStringStatic());
				}
			}
			break;

			case Network::ConsistencyInfo: {
				int clientProtocol = _data.getInt(32);
				if(clientProtocol != Network::protocolVersion) {
					network.disconnect(_id, Network::IncompatibleProtocol);
				}

				if(!gusGame.checkCRCs(_data) && network.checkCRC) // We call checkCRC anyway so that the stream is advanced
					network.disconnect(_id, Network::IncompatibleData);

			}
			break;
	}
}

void Server::Net_cbConnectionSpawned( Net_ConnID _id )
{
	console.addLogMsg("* CONNECTION SPAWNED");
	network.incConnCount();

	
	
	std::auto_ptr<Net_BitStream> data(new Net_BitStream);
	Encoding::encode(*data, Network::ClientEvents::LuaEvents, Network::ClientEvents::Max);
	network.encodeLuaEvents(data.get());
	Net_sendData ( _id, data.release(), eNet_ReliableOrdered);
}

void Server::Net_cbConnectionClosed(Net_ConnID _id, eNet_CloseReason _reason, Net_BitStream &_reasondata)
{
	console.addLogMsg("* A CONNECTION WAS CLOSED");
	for ( std::list<CWormInputHandler*>::iterator iter = game.players.begin(); iter != game.players.end(); iter++) {
		if ( (*iter)->getConnectionID() == _id ) {
			(*iter)->quit();
		}
	}
	network.decConnCount();
	DLOG("A connection was closed");
}

