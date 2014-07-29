// Standard headers.
#include <stdlib.h> // rand().
#include <time.h> // time()

#include <good/string_buffer.h>

// Good headers.
#include <good/file.h>

// Plugin headers.
#include "bot.h"
#include "clients.h"
#include "config.h"
#include "console_commands.h"
#include "item.h"
#include "event.h"
#include "icvar.h"
#include "server_plugin.h"
#include "source_engine.h"
#include "mod.h"
#include "waypoint.h"

// Source headers.
// Ugly fix for Source Engine.
#include "public/minmax.h"
#include "cbase.h"
#include "filesystem.h"
#include "interface.h"
#include "eiface.h"
#include "iplayerinfo.h"
#include "convar.h"
#include "IEngineTrace.h"
#include "IEffects.h"
#include "ndebugoverlay.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


#define BUFFER_LOG_SIZE 2048

char szMainBufferArray[BUFFER_LOG_SIZE];
char szLogBufferArray[BUFFER_LOG_SIZE];  // Static buffers for different string purposes.

int iMainBufferSize = BUFFER_LOG_SIZE;
char* szMainBuffer = szMainBufferArray;

// For logging purpuses.
int iLogBufferSize = BUFFER_LOG_SIZE;
char* szLogBuffer = szLogBufferArray;

//----------------------------------------------------------------------------------------------------------------
// The plugin is a static singleton that is exported as an interface.
//----------------------------------------------------------------------------------------------------------------
static CBotrixPlugin g_cBotrixPlugin;
CBotrixPlugin* CBotrixPlugin::instance = &g_cBotrixPlugin;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CBotrixPlugin, IServerPluginCallbacks, INTERFACEVERSION_ISERVERPLUGINCALLBACKS, g_cBotrixPlugin);

//----------------------------------------------------------------------------------------------------------------
// CBotrixPlugin static members.
//----------------------------------------------------------------------------------------------------------------
int CBotrixPlugin::iFPS = 60;
float CBotrixPlugin::fTime = 0.0f;
float CBotrixPlugin::fEngineTime = 0.0f;
float CBotrixPlugin::m_fFpsEnd = 0.0f;
int CBotrixPlugin::m_iFramesCount = 60;


//----------------------------------------------------------------------------------------------------------------
// Interfaces from the CBotrixPlugin::pEngineServer.
//----------------------------------------------------------------------------------------------------------------
IVEngineServer* CBotrixPlugin::pEngineServer = NULL;
IFileSystem* pFileSystem = NULL;

#ifdef USE_OLD_GAME_EVENT_MANAGER
IGameEventManager* CBotrixPlugin::pGameEventManager = NULL;
#else
IGameEventManager2* CBotrixPlugin::pGameEventManager = NULL;
#endif
IPlayerInfoManager* CBotrixPlugin::pPlayerInfoManager = NULL;
IServerPluginHelpers* CBotrixPlugin::pServerPluginHelpers = NULL;
IServerGameClients* CBotrixPlugin::pServerGameClients = NULL;
//IServerGameEnts* CBotrixPlugin::pServerGameEnts;
IEngineTrace* CBotrixPlugin::pEngineTrace = NULL;
IEffects* pEffects = NULL;
IBotManager* CBotrixPlugin::pBotManager = NULL;
//CGlobalVars* CBotrixPlugin::pGlobalVars = NULL;
ICvar* CBotrixPlugin::pCvar = NULL;

IVDebugOverlay* pVDebugOverlay = NULL;

//----------------------------------------------------------------------------------------------------------------
// Plugin functions.
//----------------------------------------------------------------------------------------------------------------
#define LOAD_INTERFACE(var,type,version) \
    if ((var =(type*)pInterfaceFactory(version, NULL)) == NULL ) {\
        BLOG_W("[Botrix] Cannot open interface " version " " #type " " #var "\n");\
        return false;\
    }

#define LOAD_INTERFACE_IGNORE_ERROR(var,type,version) \
    if ((var =(type*)pInterfaceFactory(version, NULL)) == NULL ) {\
        BLOG_W("[Botrix] Cannot open interface " version " " #type " " #var "\n");\
    }

#define LOAD_GAME_SERVER_INTERFACE(var, type, version) \
    if ((var =(type*)pGameServerFactory(version, NULL)) == NULL ) {\
        BLOG_W("[Botrix] Cannot open game server interface " version " " #type " " #var "\n");\
        return false;\
    }

//----------------------------------------------------------------------------------------------------------------
CBotrixPlugin::CBotrixPlugin(): bIsLoaded(false) {}

//----------------------------------------------------------------------------------------------------------------
CBotrixPlugin::~CBotrixPlugin() {}

//----------------------------------------------------------------------------------------------------------------
// Called when the plugin is loaded. Load all needed interfaces using engine.
//----------------------------------------------------------------------------------------------------------------
bool CBotrixPlugin::Load( CreateInterfaceFn pInterfaceFactory, CreateInterfaceFn pGameServerFactory )
{
    good::log::bLogToStdOut = false; // Disable log to stdout, Msg() will print there.
    good::log::bLogToStdErr = false; // Disable log to stderr, Warning() will print there.
    good::log::iStdErrLevel = good::ELogLevelWarning; // Log warnings and errors to stderr.
    good::log::iLogLevel = good::ELogLevelWarning;
    good::log::set_prefix("[Botrix] ");

#ifndef DONT_USE_VALVE_FUNCTIONS
    LOAD_GAME_SERVER_INTERFACE(pPlayerInfoManager,IPlayerInfoManager,INTERFACEVERSION_PLAYERINFOMANAGER);

    //pGlobalVars = pPlayerInfoManager->GetGlobalVars();

    LOAD_INTERFACE(pEngineServer, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
    LOAD_INTERFACE(pServerPluginHelpers, IServerPluginHelpers, INTERFACEVERSION_ISERVERPLUGINHELPERS);
    LOAD_INTERFACE(pEngineTrace, IEngineTrace, INTERFACEVERSION_ENGINETRACE_SERVER);
    LOAD_GAME_SERVER_INTERFACE(pEffects, IEffects, IEFFECTS_INTERFACE_VERSION);
    LOAD_GAME_SERVER_INTERFACE(pBotManager, IBotManager, INTERFACEVERSION_PLAYERBOTMANAGER);

    LOAD_INTERFACE_IGNORE_ERROR(pVDebugOverlay, IVDebugOverlay, VDEBUG_OVERLAY_INTERFACE_VERSION);
#ifdef USE_OLD_GAME_EVENT_MANAGER
    LOAD_INTERFACE(pGameEventManager, IGameEventManager,  INTERFACEVERSION_GAMEEVENTSMANAGER);
#else
    LOAD_INTERFACE(pGameEventManager, IGameEventManager2, INTERFACEVERSION_GAMEEVENTSMANAGER2);
#endif

    LOAD_GAME_SERVER_INTERFACE(pServerGameClients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
    LOAD_INTERFACE_IGNORE_ERROR(pFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
    //LOAD_GAME_SERVER_INTERFACE(pServerGameEnts, IServerGameEnts, INTERFACEVERSION_SERVERGAMECLIENTS);

#ifdef SOURCE_ENGINE_2006
    LOAD_INTERFACE(pCvar, ICvar, VENGINE_CVAR_INTERFACE_VERSION);
#else
    LOAD_INTERFACE(pCvar, ICvar, CVAR_INTERFACE_VERSION);
#endif

#endif // DONT_USE_VALVE_FUNCTIONS


    // Get game/mod directories.
#ifdef DONT_USE_VALVE_FUNCTIONS
    #define xstr(a) str(a)
    #define str(a) #a
    strcpy(szMainBuffer, xstr(DONT_USE_VALVE_FUNCTIONS)); // Mod directory.
#else
    pEngineServer->GetGameDir(szMainBuffer, iMainBufferSize);
#endif
    sModFolder = szMainBuffer;
    int modPos = sModFolder.rfind(PATH_SEPARATOR);
    sModFolder = good::string(&szMainBuffer[modPos+1], true, true);

    if ( pFileSystem )
        pFileSystem->GetCurrentDirectory(szMainBuffer, iMainBufferSize);
    else
        szMainBuffer[modPos] = 0; // Remove trailing mod name (for example hl2dm).

    sGameFolder = szMainBuffer;
    int gamePos = sGameFolder.rfind(PATH_SEPARATOR);
    szMainBuffer[gamePos] = 0;
    sGameFolder = good::string(&szMainBuffer[gamePos+1], true, true);
    good::lower_case(sGameFolder);

    sBotrixPath = szMainBuffer;
    sBotrixPath = good::file::append_path( sBotrixPath, good::string("botrix") );

    // Load configuration file.
    good::string iniName( good::file::append_path(sBotrixPath, good::string("config.ini") ) );
    TModId iModId = CConfiguration::Load(iniName, sGameFolder, sModFolder);

    // Create console command instance.
    CMainCommand::instance = good::unique_ptr<CMainCommand>( new CMainCommand() );

    // Load mod configuration.
    CMod::Load(iModId);

#ifdef SOURCE_ENGINE_2006
    MathLib_Init(1.0, 1.0, 1.0, 1.0, false, true, false, true);
#else
    MathLib_Init();
#endif
    srand( time(NULL) );

    bIsLoaded = true;
    const char* sMod = CMod::sModName.c_str();
    if ( CMod::sModName.size() == 0 )
        sMod = "unknown";

    BLOG_I("Botrix loaded. Current mod: %s.", sMod);

    return true;
}

//----------------------------------------------------------------------------------------------------------------
// Called when the plugin is unloaded
//----------------------------------------------------------------------------------------------------------------
void CBotrixPlugin::Unload( void )
{
    CConfiguration::Save();
    CConfiguration::Unload();

    CMainCommand::instance.reset();

    if ( pGameEventManager )
        pGameEventManager->RemoveListener(this);

    good::log::stop_log_to_file();
    bIsLoaded = false;
}

//----------------------------------------------------------------------------------------------------------------
// Called when the plugin is paused(i.e should stop running but isn't unloaded)
//----------------------------------------------------------------------------------------------------------------
void CBotrixPlugin::Pause( void )
{
}

//----------------------------------------------------------------------------------------------------------------
// Called when the plugin is unpaused(i.e should start executing again)
//----------------------------------------------------------------------------------------------------------------
void CBotrixPlugin::UnPause( void )
{
}

//----------------------------------------------------------------------------------------------------------------
// the name of this plugin, returned in "plugin_print" command
//----------------------------------------------------------------------------------------------------------------
const char* CBotrixPlugin::GetPluginDescription( void )
{
    return "Botrix plugin " PLUGIN_VERSION " (c) 2012 by Borzh. BUILD " __DATE__;
}

//----------------------------------------------------------------------------------------------------------------
// Called on level start
//----------------------------------------------------------------------------------------------------------------
void CBotrixPlugin::LevelInit( const char* pMapName )
{
    sMapName = pMapName;

#ifndef DONT_USE_VALVE_FUNCTIONS
    ConVar *pTeamplay = pCvar->FindVar("mp_teamplay");
    bTeamPlay = pTeamplay ? pTeamplay->GetBool() : false;

#ifdef USE_OLD_GAME_EVENT_MANAGER
    pGameEventManager->AddListener( this, true );
#endif

    CWaypoint::iWaypointTexture = CBotrixPlugin::pEngineServer->PrecacheModel( "sprites/lgtning.vmt" );
#endif

    CWaypoints::Load();
}

//----------------------------------------------------------------------------------------------------------------
// Called on level start, when the server is ready to accept client connections
// edictCount is the number of entities in the level, clientMax is the max client count
//----------------------------------------------------------------------------------------------------------------
void CBotrixPlugin::ServerActivate( edict_t* /*pEdictList*/, int /*edictCount*/, int clientMax )
{
    bMapRunning = true;

    CPlayers::Init(clientMax);
    CItems::MapLoaded();
    CMod::MapLoaded();

    BLOG_I("Level \"%s\" has been loaded.", sMapName.c_str());
}

//----------------------------------------------------------------------------------------------------------------
// Called once per server frame, do recurring work here(like checking for timeouts)
//----------------------------------------------------------------------------------------------------------------
void CBotrixPlugin::GameFrame( bool /*simulating*/ )
{
    CUtil::PrintMessagesInQueue();

    float fPrevEngineTime = fEngineTime;
    fEngineTime = pEngineServer->Time();

    float fDiff = fEngineTime - fPrevEngineTime;
    if ( fDiff > 1.0f ) // Too low fps, possibly debugging.
        fTime += 0.1f;
    else
        fTime += fDiff;

    // FPS counting. Used in draw waypoints. TODO: define.
    /*m_iFramesCount++;
    if (fEngineTime >= m_fFpsEnd)
    {
        iFPS = m_iFramesCount;
        m_iFramesCount = 0;
        m_fFpsEnd = fEngineTime + 1.0f;
        //BULOG_T(NULL, "FPS: %d", iFPS);
    }*/

    if ( bMapRunning )
    {
        if ( CMod::pCurrentMod )
            CMod::pCurrentMod->Think();

        // Show fps.
        //m_iFramesCount++;
        //if (fTime >= m_fFpsEnd)
        //{
        //	iFPS = m_iFramesCount;
        //	m_iFramesCount = 0;
        //	m_fFpsEnd = fTime + 1.0f;
        //	BULOG_T(NULL, "FPS: %d", iFPS);
        //}

        CItems::Update();
        CPlayers::PreThink();

//#define BOTRIX_SHOW_PERFORMANCE
#ifdef BOTRIX_SHOW_PERFORMANCE
        static const float fInterval = 10.0f; // Print & reset every 10 seconds.
        static float fStart = 0.0f, fSum = 0.0f;
        static int iCount = 0;

        if ( fStart == 0.0f )
            fStart = fEngineTime;

        fSum += pEngineServer->Time() - fEngineTime;
        iCount++;

        if ( (fStart + fInterval) <= pEngineServer->Time() )
        {
            BLOG_T("Botrix think time in %d frames (%.0f seconds): %.5f msecs",
                   iCount, fInterval, fSum / (float)iCount * 1000.0f);
            fStart = fSum = 0.0f;
            iCount = 0;
        }
#endif
    }
}

//----------------------------------------------------------------------------------------------------------------
// Called on level end (as the server is shutting down or going to a new map)
//----------------------------------------------------------------------------------------------------------------
void CBotrixPlugin::LevelShutdown( void ) // !!!!this can get called multiple times per map change
{
    bMapRunning = false;

    CPlayers::Clear();
    CWaypoints::Clear();
    CItems::MapUnloaded();

#ifdef USE_OLD_GAME_EVENT_MANAGER
    pGameEventManager->RemoveListener(this);
#endif

    CConfiguration::Save();
}

//----------------------------------------------------------------------------------------------------------------
// Called when a client spawns into a server (i.e as they begin to play)
//----------------------------------------------------------------------------------------------------------------
void CBotrixPlugin::ClientActive( edict_t* pEntity )
{
    CPlayers::PlayerConnected(pEntity);
}

//----------------------------------------------------------------------------------------------------------------
// Called when a client leaves a server(or is timed out)
//----------------------------------------------------------------------------------------------------------------
void CBotrixPlugin::ClientDisconnect( edict_t* pEntity )
{
    CPlayers::PlayerDisconnected(pEntity);
}

//----------------------------------------------------------------------------------------------------------------
// Called on client being added to this server
//----------------------------------------------------------------------------------------------------------------
void CBotrixPlugin::ClientPutInServer( edict_t* /*pEntity*/, const char* /*playername*/ )
{
}

//----------------------------------------------------------------------------------------------------------------
// Sets the client index for the client who typed the command into their console
//----------------------------------------------------------------------------------------------------------------
void CBotrixPlugin::SetCommandClient( int /*index*/ )
{
    //m_iClientCommandIndex = index;
}

//----------------------------------------------------------------------------------------------------------------
// A player changed one/several replicated cvars (name etc)
//----------------------------------------------------------------------------------------------------------------
void CBotrixPlugin::ClientSettingsChanged( edict_t* /*pEdict*/ )
{
    //const char* name = pEngineServer->GetClientConVarValue( pEngineServer->IndexOfEdict(pEdict), "name" );
}

//----------------------------------------------------------------------------------------------------------------
// Called when a client joins a server.
//----------------------------------------------------------------------------------------------------------------
PLUGIN_RESULT CBotrixPlugin::ClientConnect( bool */*bAllowConnect*/, edict_t* /*pEntity*/, const char */*pszName*/,
                                            const char */*pszAddress*/, char */*reject*/, int /*maxrejectlen*/ )
{
    return PLUGIN_CONTINUE;
}

//----------------------------------------------------------------------------------------------------------------
// Called when a client types in a command (only a subset of commands however, not CON_COMMAND's)
//----------------------------------------------------------------------------------------------------------------
#ifdef SOURCE_ENGINE_2006
PLUGIN_RESULT CBotrixPlugin::ClientCommand( edict_t* pEntity )
#else
PLUGIN_RESULT CBotrixPlugin::ClientCommand( edict_t* pEntity, const CCommand &args )
#endif
{
    BASSERT( pEntity && !pEntity->IsFree(), return PLUGIN_CONTINUE ); // Valve check.

#ifdef SOURCE_ENGINE_2006
    int argc = MIN2(CBotrixPlugin::pEngineServer->Cmd_Argc(), 16);

    if ( (argc == 0) || !CMainCommand::instance->IsCommand( CBotrixPlugin::pEngineServer->Cmd_Argv(0) ) )
        return PLUGIN_CONTINUE;

    static const char* argv[16];
    for (int i = 0; i < argc; ++i)
        argv[i] = CBotrixPlugin::pEngineServer->Cmd_Argv(i);
#else
    int argc = args.ArgC();
    const char** argv = args.ArgV();

    if ( (argc == 0) || !CMainCommand::instance->IsCommand( argv[0] ) )
        return PLUGIN_CONTINUE; // Not a "botrix" command.
#endif

    int iIdx = CPlayers::Get(pEntity);
    BASSERT(iIdx >= 0, return PLUGIN_CONTINUE);

    CPlayer* pPlayer = CPlayers::Get(iIdx);
    BASSERT(pPlayer && !pPlayer->IsBot(), return PLUGIN_CONTINUE); // Valve check.

    CClient* pClient = (CClient*)pPlayer;

    TCommandResult iResult = CMainCommand::instance->Execute( pClient, argc-1, &argv[1] );

    if (iResult != ECommandPerformed)
    {
        if (iResult == ECommandRequireAccess)
            BULOG_E(pClient ? pClient->GetEdict() : NULL, "Sorry, you don't have access to this command.");
        else if (iResult == ECommandNotFound)
            BULOG_E(pClient ? pClient->GetEdict() : NULL, "Command not found.");
        else if (iResult == ECommandError)
            BULOG_E(pClient ? pClient->GetEdict() : NULL, "Command error.");
    }
    return PLUGIN_STOP; // We handled this command.
}

//----------------------------------------------------------------------------------------------------------------
void CBotrixPlugin::OnEdictAllocated( edict_t *edict )
{
    CItems::Allocated(edict);
}

//----------------------------------------------------------------------------------------------------------------
void CBotrixPlugin::OnEdictFreed( const edict_t *edict  )
{
    CItems::Freed(edict);
}

//----------------------------------------------------------------------------------------------------------------
// Called when a client is authenticated
//----------------------------------------------------------------------------------------------------------------
PLUGIN_RESULT CBotrixPlugin::NetworkIDValidated( const char *pszUserName, const char *pszNetworkID )
{
    TCommandAccessFlags iAccess = CConfiguration::ClientAccessLevel(pszNetworkID);
    if ( iAccess ) // Founded.
    {
        for ( int i = 0; i < CPlayers::GetPlayersCount(); ++i )
        {
            CPlayer* pPlayer = CPlayers::Get(i);
            if ( pPlayer && !pPlayer->IsBot() )
            {
                CClient* pClient = (CClient*)pPlayer;
                if ( good::string(pszNetworkID) == pClient->GetSteamID() )
                {
                    pClient->iCommandAccessFlags = iAccess;
                    break;
                }
            }
        }
    }

    BLOG_I( "User id validated %s (steam id %s), access: %s.", pszUserName, pszNetworkID,
            CTypeToString::AccessFlagsToString(iAccess).c_str() );

    return PLUGIN_CONTINUE;
}

//----------------------------------------------------------------------------------------------------------------
// Called when an event is fired
//----------------------------------------------------------------------------------------------------------------
#ifdef USE_OLD_GAME_EVENT_MANAGER
void CBotrixPlugin::FireGameEvent( KeyValues* event )
{
    good::string_buffer sbBuffer(szMainBuffer, iMainBufferSize, false);

    const char* type = event->GetName();

    if ( CPlayers::IsDebuggingEvents() )
    {
        KeyValues* copy = event->MakeCopy(); // Source bug? Heap corrupted if iterate directly on event.

        sbBuffer << "Event: " << type << '\n';

        for ( KeyValues *pk = copy->GetFirstTrueSubKey(); pk; pk = pk->GetNextTrueSubKey() )
            sbBuffer << pk->GetName() << '\n';

        for ( KeyValues *pk = copy->GetFirstValue(); pk; pk = pk->GetNextValue() )
            sbBuffer << pk->GetName() << " = " << pk->GetString() << '\n';

        CPlayers::DebugEvent(szMainBuffer);

        copy->deleteThis();
    }

    CMod::ExecuteEvent( (void*)event, EEventTypeKeyValues );
}

void CBotrixPlugin::GenerateSayEvent( edict_t* pEntity, const char* szText, bool bTeamOnly )
{
    int iUserId = pEngineServer->GetPlayerUserId(pEntity);
    BASSERT(iUserId != -1, return);

    KeyValues* pEvent = pGameEventManager->GetEvent("player_say", true);
    if (pEvent)
    {
        BASSERT( strcmp(pEvent->GetName(), "player_say") ); // Valve check.
        pEvent->SetInt("userid", iUserId);
        pEvent->SetString("text", szText);
        pEvent->SetBool("teamonly", bTeamOnly);
        pGameEventManager->FireEvent(pEvent);
        //pEvent->deleteThis(); // TODO: need to delete?
    }
}

#else // USE_OLD_GAME_EVENT_MANAGER

void CBotrixPlugin::FireGameEvent( IGameEvent * event )
{
    if ( CPlayers::IsDebuggingEvents() )
        CPlayers::DebugEvent( "Event: %s", event->GetName() );

    CMod::ExecuteEvent( (void*)event, EEventTypeIGameEvent );
}

void CBotrixPlugin::GenerateSayEvent( edict_t* pEntity, const char* szText, bool bTeamOnly )
{
    int iUserId = pEngineServer->GetPlayerUserId(pEntity);
    BASSERT(iUserId != -1, return);

    IGameEvent* pEvent = pGameEventManager->CreateEvent("player_say");
    if (pEvent)
    {
        pEvent->SetInt("userid", iUserId);
        pEvent->SetString("text", szText);
        pEvent->SetBool("teamonly", bTeamOnly);
        //pEvent->SetInt("priority", 1 );	// HLTV event priority, not transmitted
        pGameEventManager->FireEvent(pEvent);
        //pGameEventManager2->FreeEvent(pEvent); // Don't free it !!!
    }
}

#endif


void CBotrixPlugin::HudTextMessage( edict_t* pEntity, char */*szTitle*/, char *szMessage, Color colour, int level, int time )
{
    KeyValues *kv = new KeyValues( "menu" );
    kv->SetString( "title", szMessage );
    //kv->SetString( "msg", szMessage );
    kv->SetColor( "color", colour);
    kv->SetInt( "level", level);
    kv->SetInt( "time", time);
    //DIALOG_TEXT
    pServerPluginHelpers->CreateMessage( pEntity, DIALOG_MSG, kv, instance );

    kv->deleteThis();
}
