//=============================================================================
// TF2 Dungeon Mode (spike): no weapons, players cast spells, PvE floors of monsters.
//
//   MOUSE1  Fireball          (0.6s cooldown)
//   MOUSE2  Ka-me-ha-me-ha    hold to charge, release to fire a piercing beam
//   RELOAD  Lightning orb     (10s cooldown)
//   MOUSE3  Overheal          (15s cooldown)
//
// Console: tf_dungeon_status (director + monster state), tf_dungeon_set_floor N (cheat).
//
// The director spawns "floors" of skeletons around the RED party; every Nth floor
// adds a boss. Clear a floor -> fallen allies revive -> next floor. Party wipe -> floor 1.
//=============================================================================
#include "cbase.h"
#include "igamesystem.h"
#include "in_buttons.h"
#include "tf_player.h"
#include "tf_gamerules.h"
#include "tf_team.h"
#include "tf_fx.h"
#include "particle_parse.h"
#include "nav_mesh.h"
#include "nav_mesh/tf_nav_area.h"
#include "halloween/zombie/zombie.h"
#include "halloween/halloween_base_boss.h"
#include "halloween/tf_weapon_spellbook.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar tf_dungeon_mode;
extern ConVar mp_humans_must_join_team;

ConVar tf_dungeon_base_monsters( "tf_dungeon_base_monsters", "4", FCVAR_NOTIFY, "Skeletons on floor 1; +2 per floor." );
ConVar tf_dungeon_max_monsters( "tf_dungeon_max_monsters", "24", FCVAR_NOTIFY, "Cap on skeletons per floor." );
ConVar tf_dungeon_boss_every( "tf_dungeon_boss_every", "3", FCVAR_NOTIFY, "Every Nth floor is a boss floor." );
ConVar tf_dungeon_floor_delay( "tf_dungeon_floor_delay", "8", FCVAR_NOTIFY, "Seconds of rest between floors." );
ConVar tf_dungeon_beam_damage( "tf_dungeon_beam_damage", "400", FCVAR_NOTIFY, "Ka-me-ha-me-ha damage at full charge." );

#define BEAM_CHARGE_TIME	1.5f
#define BEAM_RANGE			4096.f

//=============================================================================
// Spells
//=============================================================================
// ponytail: per-entindex arrays instead of networked player fields; fine while spells are server-only.
static float s_flNextFireball[ MAX_PLAYERS + 1 ];
static float s_flNextBeam[ MAX_PLAYERS + 1 ];
static float s_flNextOrb[ MAX_PLAYERS + 1 ];
static float s_flNextHeal[ MAX_PLAYERS + 1 ];
static float s_flBeamChargeStart[ MAX_PLAYERS + 1 ];	// 0 = not charging
static int   s_iBeamChargeStep[ MAX_PLAYERS + 1 ];

static void ResetSpellState()
{
	for ( int i = 0; i <= MAX_PLAYERS; ++i )
	{
		s_flNextFireball[i] = s_flNextBeam[i] = s_flNextOrb[i] = s_flNextHeal[i] = s_flBeamChargeStart[i] = 0.f;
		s_iBeamChargeStep[i] = -1;
	}
}

static bool CooldownReady( CTFPlayer *pPlayer, float flNext, const char *pszName )
{
	if ( gpGlobals->curtime >= flNext )
		return true;

	ClientPrint( pPlayer, HUD_PRINTCENTER, CFmtStr( "%s recharging: %.0fs", pszName, ceilf( flNext - gpGlobals->curtime ) ) );
	return false;
}

// Same spawn recipe as CTFSpellBook::CreateSpellRocket, minus the launcher weapon.
static void LaunchSpellRocket( CTFPlayer *pPlayer, const char *pszEntity, float flSpeed, const char *pszSound )
{
	Vector vecFwd, vecRight, vecUp;
	AngleVectors( pPlayer->EyeAngles(), &vecFwd, &vecRight, &vecUp );
	Vector vecSrc = pPlayer->Weapon_ShootPosition() + vecFwd * 16.f + vecUp * -9.f;

	trace_t tr;
	UTIL_TraceHull( pPlayer->EyePosition(), vecSrc, -Vector( 8, 8, 8 ), Vector( 8, 8, 8 ), MASK_SOLID_BRUSHONLY, pPlayer, COLLISION_GROUP_NONE, &tr );
	if ( tr.startsolid )
		return;

	CTFProjectile_Rocket *pRocket = static_cast< CTFProjectile_Rocket* >( CBaseEntity::CreateNoSpawn( pszEntity, tr.endpos, pPlayer->EyeAngles(), pPlayer ) );
	if ( !pRocket )
		return;

	pRocket->SetOwnerEntity( pPlayer );
	pRocket->SetAbsVelocity( vecFwd * flSpeed );
	pRocket->SetDamage( 100.f );
	pRocket->ChangeTeam( pPlayer->GetTeamNumber() );
	DispatchSpawn( pRocket );

	pPlayer->EmitSound( pszSound );
}

static void FireKamehameha( CTFPlayer *pPlayer, float flCharge )
{
	Vector vecFwd;
	AngleVectors( pPlayer->EyeAngles(), &vecFwd );
	Vector vecSrc = pPlayer->Weapon_ShootPosition();

	trace_t tr;
	UTIL_TraceLine( vecSrc, vecSrc + vecFwd * BEAM_RANGE, MASK_SOLID_BRUSHONLY, pPlayer, COLLISION_GROUP_NONE, &tr );

	// Fat piercing beam: everything enemy along the swept box gets hit.
	float flRadius = 16.f + 32.f * flCharge;
	Ray_t ray;
	ray.Init( vecSrc, tr.endpos, -Vector( flRadius, flRadius, flRadius ), Vector( flRadius, flRadius, flRadius ) );

	CBaseEntity *pList[ 64 ];
	int nCount = UTIL_EntitiesAlongRay( pList, ARRAYSIZE( pList ), ray, FL_NPC | FL_CLIENT | FL_FAKECLIENT );

	float flDamage = tf_dungeon_beam_damage.GetFloat() * ( 0.15f + 0.85f * flCharge );
	for ( int i = 0; i < nCount; ++i )
	{
		CBaseEntity *pVictim = pList[i];
		if ( pVictim == pPlayer || !pVictim->IsAlive() || pVictim->GetTeamNumber() == pPlayer->GetTeamNumber() )
			continue;

		CTakeDamageInfo info( pPlayer, pPlayer, flDamage, DMG_ENERGYBEAM );
		info.SetDamageCustom( TF_DMG_CUSTOM_SPELL_LIGHTNING );
		info.SetDamagePosition( pVictim->WorldSpaceCenter() );
		info.SetDamageForce( vecFwd * 20000.f * flCharge );
		pVictim->TakeDamage( info );
	}

	// Visuals: Machina rail as the beam, Cow Mangler charged blast at the end.
	UTIL_ParticleTracer( "dxhr_sniper_rail_blue", vecSrc + vecFwd * 24.f + Vector( 0, 0, -12 ), tr.endpos, pPlayer->entindex() );
	CPVSFilter filter( tr.endpos );
	TE_TFParticleEffect( filter, 0.f, "drg_cow_explosioncore_charged_blue", tr.endpos, vec3_angle );
	pPlayer->EmitSound( "Weapon_SniperRailgun_Large.SingleCrit" );
	ClientPrint( pPlayer, HUD_PRINTCENTER, "HAAAAAAAAAAA!!!" );
}

static void UpdateKamehameha( CTFPlayer *pPlayer, int idx )
{
	bool bHeld = ( pPlayer->m_nButtons & IN_ATTACK2 ) != 0;

	if ( s_flBeamChargeStart[idx] == 0.f )
	{
		if ( !( pPlayer->m_afButtonPressed & IN_ATTACK2 ) || !CooldownReady( pPlayer, s_flNextBeam[idx], "Ka-me-ha-me-ha" ) )
			return;

		s_flBeamChargeStart[idx] = gpGlobals->curtime;
		s_iBeamChargeStep[idx] = -1;
		CPVSFilter filter( pPlayer->GetAbsOrigin() );
		TE_TFParticleEffect( filter, 0.f, "spell_cast_wheel_blue", PATTACH_ABSORIGIN_FOLLOW, pPlayer );
		pPlayer->EmitSound( "Halloween.spell_lightning_cast" );
	}

	float flCharge = MIN( 1.f, ( gpGlobals->curtime - s_flBeamChargeStart[idx] ) / BEAM_CHARGE_TIME );

	if ( bHeld )
	{
		static const char *s_pszChant[] = { "KA...", "KA... ME...", "KA... ME... HA...", "KA... ME... HA... ME...", "KA... ME... HA... ME... (release!)" };
		int iStep = (int)( flCharge * 4.f );
		if ( iStep != s_iBeamChargeStep[idx] )
		{
			s_iBeamChargeStep[idx] = iStep;
			ClientPrint( pPlayer, HUD_PRINTCENTER, s_pszChant[ iStep ] );
		}
		return;
	}

	// Released.
	s_flBeamChargeStart[idx] = 0.f;
	FireKamehameha( pPlayer, flCharge );
	s_flNextBeam[idx] = gpGlobals->curtime + 0.8f;
}

void TFDungeon_PlayerPostThink( CTFPlayer *pPlayer )
{
	int idx = pPlayer->entindex();
	if ( idx <= 0 || idx > MAX_PLAYERS )
		return;

	if ( !pPlayer->IsAlive() || !pPlayer->CanAttack() || pPlayer->GetTeamNumber() < FIRST_GAME_TEAM )
	{
		s_flBeamChargeStart[idx] = 0.f;
		return;
	}

	UpdateKamehameha( pPlayer, idx );
	if ( s_flBeamChargeStart[idx] != 0.f )
		return;	// concentrating

	int nPressed = pPlayer->m_afButtonPressed;
	if ( ( pPlayer->m_nButtons & IN_ATTACK ) && gpGlobals->curtime >= s_flNextFireball[idx] )
	{
		LaunchSpellRocket( pPlayer, "tf_projectile_spellfireball", 1100.f, "Halloween.spell_fireball_cast" );
		s_flNextFireball[idx] = gpGlobals->curtime + 0.6f;
	}
	else if ( ( nPressed & IN_RELOAD ) && CooldownReady( pPlayer, s_flNextOrb[idx], "Lightning orb" ) )
	{
		LaunchSpellRocket( pPlayer, "tf_projectile_lightningorb", 440.f, "Halloween.spell_lightning_cast" );
		s_flNextOrb[idx] = gpGlobals->curtime + 10.f;
	}
	else if ( ( nPressed & IN_ATTACK3 ) && CooldownReady( pPlayer, s_flNextHeal[idx], "Overheal" ) )
	{
		pPlayer->EmitSound( "Halloween.spell_overheal" );
		CTFSpellBook::CastSelfHeal( pPlayer );
		s_flNextHeal[idx] = gpGlobals->curtime + 15.f;
	}
}

//=============================================================================
// Director
//=============================================================================
class CTFDungeonDirector : public CAutoGameSystemPerFrame
{
public:
	CTFDungeonDirector() : CAutoGameSystemPerFrame( "CTFDungeonDirector" ) {}

	virtual void LevelInitPreEntity() OVERRIDE
	{
		Reset();
		ResetSpellState();
		m_flNavCheckTime = 0.f;
		PrecacheParticleSystem( "dxhr_sniper_rail_blue" );
		PrecacheParticleSystem( "drg_cow_explosioncore_charged_blue" );
	}

	virtual void LevelInitPostEntity() OVERRIDE
	{
		if ( tf_dungeon_mode.GetBool() )
		{
			mp_humans_must_join_team.SetValue( "red" );	// PvE: the party is always RED
		}
	}

	virtual void FrameUpdatePostEntityThink() OVERRIDE;

	void PrintStatus()
	{
		Msg( "[Dungeon] mode=%d floor=%d active=%d monsters=%d next_floor_in=%.1f round_state=%d waiting=%d nav=%d\n",
			tf_dungeon_mode.GetInt(), m_iFloor, m_bFloorActive, m_monsters.Count(),
			m_flNextFloorTime ? m_flNextFloorTime - gpGlobals->curtime : -1.f,
			TFGameRules() ? (int)TFGameRules()->State_Get() : -1,
			TFGameRules() ? (int)TFGameRules()->IsInWaitingForPlayers() : -1, (int)TheNavMesh->IsLoaded() );

		CUtlVector< CTFPlayer* > party;
		CollectPlayers( &party, TF_TEAM_RED, COLLECT_ONLY_LIVING_PLAYERS );
		FOR_EACH_VEC( m_monsters, i )
		{
			CBaseEntity *pMonster = m_monsters[i];
			if ( !pMonster )
				continue;
			float flNearest = FLT_MAX;
			FOR_EACH_VEC( party, j )
			{
				flNearest = MIN( flNearest, ( party[j]->GetAbsOrigin() - pMonster->GetAbsOrigin() ).Length() );
			}
			Msg( "[Dungeon]   %s hp=%d at (%.0f %.0f %.0f) nearest_player=%.0f\n", pMonster->GetClassname(), pMonster->GetHealth(),
				pMonster->GetAbsOrigin().x, pMonster->GetAbsOrigin().y, pMonster->GetAbsOrigin().z, flNearest );
		}
		FOR_EACH_VEC( party, j )
		{
			Msg( "[Dungeon]   player %s hp=%d at (%.0f %.0f %.0f) eyes (%.1f %.1f)\n", party[j]->GetPlayerName(), party[j]->GetHealth(),
				party[j]->GetAbsOrigin().x, party[j]->GetAbsOrigin().y, party[j]->GetAbsOrigin().z,
				party[j]->EyeAngles()[PITCH], party[j]->EyeAngles()[YAW] );
		}
	}

	// Cheat: clear the current floor and make the next one floor N.
	void SetFloor( int iFloor )
	{
		FOR_EACH_VEC( m_monsters, i )
		{
			UTIL_Remove( m_monsters[i] );
		}
		m_monsters.RemoveAll();
		m_iFloor = MAX( 0, iFloor - 1 );
		m_bFloorActive = false;
		m_flNextFloorTime = gpGlobals->curtime + 1.f;
	}

private:
	void Reset()
	{
		m_iFloor = 0;
		m_bFloorActive = false;
		m_flNextFloorTime = 0.f;
		m_monsters.RemoveAll();
	}

	void Announce( const char *pszMsg )
	{
		UTIL_CenterPrintAll( pszMsg );
		UTIL_ClientPrintAll( HUD_PRINTTALK, CFmtStr( "[Dungeon] %s", pszMsg ) );
		Msg( "[Dungeon] %s\n", pszMsg );
	}

	bool FindSpawnPos( const CUtlVector< CTFPlayer* > &party, Vector &vecPos );
	void SpawnFloor( const CUtlVector< CTFPlayer* > &party );
	void EnsureNavMesh();

	int m_iFloor;
	bool m_bFloorActive;
	float m_flNextFloorTime;
	float m_flNavCheckTime;
	CUtlVector< EHANDLE > m_monsters;
};

static CTFDungeonDirector g_TFDungeonDirector;

CON_COMMAND( tf_dungeon_status, "Print the dungeon director state." )
{
	g_TFDungeonDirector.PrintStatus();
}

CON_COMMAND_F( tf_dungeon_set_floor, "Skip to dungeon floor N (clears the current floor).", FCVAR_CHEAT )
{
	if ( args.ArgC() < 2 )
	{
		Msg( "Usage: tf_dungeon_set_floor <floor>\n" );
		return;
	}
	g_TFDungeonDirector.SetFloor( atoi( args[1] ) );
}

void CTFDungeonDirector::EnsureNavMesh()
{
	if ( TheNavMesh->IsLoaded() || TheNavMesh->IsGenerating() )
		return;

	if ( m_flNavCheckTime == 0.f )
	{
		m_flNavCheckTime = gpGlobals->curtime + 5.f;
		return;
	}

	if ( gpGlobals->curtime < m_flNavCheckTime )
		return;

	// Monsters need a nav mesh to walk. Build one; the engine reloads the map when done.
	Announce( "This map has no nav mesh. Carving the dungeon (generating nav, map will reload)..." );
	TheNavMesh->BeginGeneration();
	m_flNavCheckTime = FLT_MAX;
}

bool CTFDungeonDirector::FindSpawnPos( const CUtlVector< CTFPlayer* > &party, Vector &vecPos )
{
	// Sample the whole mesh by straight-line distance. Walking out from the players' areas fails
	// while they sit in a spawn room, and monsters can't enter spawn rooms anyway.
	// ponytail: 64 random samples per monster, not a precomputed candidate list; fine at <=25 spawns per floor.
	if ( !TheNavAreas.Count() )
		return false;

	CTFNavArea *pBest = NULL;
	float flBestSq = FLT_MAX;
	for ( int nTry = 0; nTry < 64; ++nTry )
	{
		CTFNavArea *pArea = static_cast< CTFNavArea* >( TheNavAreas[ RandomInt( 0, TheNavAreas.Count() - 1 ) ] );
		if ( pArea->HasAttributeTF( TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE ) )
			continue;

		float flNearestSq = FLT_MAX;
		FOR_EACH_VEC( party, i )
		{
			flNearestSq = MIN( flNearestSq, ( party[i]->GetAbsOrigin() - pArea->GetCenter() ).LengthSqr() );
		}

		if ( flNearestSq < Square( 600.f ) )
			continue;	// don't pop out on top of someone

		if ( flNearestSq <= Square( 2500.f ) )
		{
			vecPos = pArea->GetCenter();
			return true;
		}

		if ( flNearestSq < flBestSq )
		{
			pBest = pArea;
			flBestSq = flNearestSq;
		}
	}

	if ( !pBest )
		return false;

	vecPos = pBest->GetCenter();
	return true;
}

void CTFDungeonDirector::SpawnFloor( const CUtlVector< CTFPlayer* > &party )
{
	int iFloor = m_iFloor + 1;
	bool bBossFloor = tf_dungeon_boss_every.GetInt() > 0 && ( iFloor % tf_dungeon_boss_every.GetInt() ) == 0;

	int nSkeletons = MIN( tf_dungeon_max_monsters.GetInt(), tf_dungeon_base_monsters.GetInt() + 2 * ( iFloor - 1 ) );
	if ( bBossFloor )
	{
		nSkeletons /= 2;
	}

	Vector vecPos;
	for ( int i = 0; i < nSkeletons; ++i )
	{
		if ( !FindSpawnPos( party, vecPos ) )
			continue;

		CZombie::SkeletonType_t eType = ( RandomInt( 0, 3 ) == 0 ) ? CZombie::SKELETON_MINI : CZombie::SKELETON_NORMAL;
		// ponytail: 180s lifetime so a skeleton stuck behind geometry can't softlock the floor.
		CZombie *pZombie = CZombie::SpawnAtPos( vecPos, 180.f, TF_TEAM_HALLOWEEN, NULL, eType );
		if ( pZombie )
		{
			m_monsters.AddToTail( pZombie );
		}
	}

	const char *pszBoss = NULL;
	if ( bBossFloor && FindSpawnPos( party, vecPos ) )
	{
		CBaseEntity *pBoss = NULL;
		switch ( ( iFloor / tf_dungeon_boss_every.GetInt() - 1 ) % 3 )
		{
		case 0:
			pBoss = CZombie::SpawnAtPos( vecPos, 0.f, TF_TEAM_HALLOWEEN, NULL, CZombie::SKELETON_KING );
			pszBoss = "the Skeleton King";
			break;
		case 1:
			pBoss = CHalloweenBaseBoss::SpawnBossAtPos( HALLOWEEN_BOSS_HHH, vecPos );
			pszBoss = "the Horseless Headless Horsemann";
			break;
		default:
			pBoss = CHalloweenBaseBoss::SpawnBossAtPos( HALLOWEEN_BOSS_MONOCULUS, vecPos );
			pszBoss = "MONOCULUS";
			break;
		}

		if ( pBoss )
		{
			// Halloween bosses lack FL_NPC (skeletons set it), so FL_NPC entity queries (beam, spell splash) would skip them.
			pBoss->AddFlag( FL_NPC );
			m_monsters.AddToTail( pBoss );
		}
	}

	if ( !m_monsters.Count() )
	{
		DevMsg( "[Dungeon] floor %d: no spawn position found, retrying\n", iFloor );
		return;	// nowhere to spawn yet; try again next frame
	}

	m_iFloor = iFloor;
	m_bFloorActive = true;
	Announce( pszBoss ? CFmtStr( "FLOOR %d: %s awaits!", m_iFloor, pszBoss ).Get()
					  : CFmtStr( "FLOOR %d: %d monsters rise from the dark!", m_iFloor, m_monsters.Count() ).Get() );
}

void CTFDungeonDirector::FrameUpdatePostEntityThink()
{
	if ( !tf_dungeon_mode.GetBool() || !TFGameRules() )
		return;

	// Each round is a fresh dungeon run. A round restart wipes our monsters anyway.
	if ( TFGameRules()->State_Get() != GR_STATE_RND_RUNNING || TFGameRules()->IsInWaitingForPlayers() )
	{
		if ( m_iFloor || m_monsters.Count() || m_flNextFloorTime != 0.f )
		{
			Reset();
		}
		return;
	}

	EnsureNavMesh();
	if ( !TheNavMesh->IsLoaded() )
		return;

	FOR_EACH_VEC_BACK( m_monsters, i )
	{
		if ( !m_monsters[i] || !m_monsters[i]->IsAlive() )
		{
			m_monsters.FastRemove( i );
		}
	}

	CUtlVector< CTFPlayer* > party;
	CollectPlayers( &party, TF_TEAM_RED, COLLECT_ONLY_LIVING_PLAYERS );
	if ( GetGlobalTFTeam( TF_TEAM_RED )->GetNumPlayers() == 0 )
		return;

	if ( m_bFloorActive )
	{
		if ( !party.Count() )
		{
			Announce( CFmtStr( "The party was wiped out on floor %d. The dungeon resets...", m_iFloor ) );
			FOR_EACH_VEC( m_monsters, i )
			{
				UTIL_Remove( m_monsters[i] );
			}
			Reset();
			m_flNextFloorTime = gpGlobals->curtime + 15.f;
			return;
		}

		if ( !m_monsters.Count() )
		{
			Announce( CFmtStr( "Floor %d cleared! Fallen allies return. Catch your breath...", m_iFloor ) );
			m_bFloorActive = false;
			m_flNextFloorTime = gpGlobals->curtime + tf_dungeon_floor_delay.GetFloat();

			CUtlVector< CTFPlayer* > everyone;
			CollectPlayers( &everyone, TF_TEAM_RED );
			FOR_EACH_VEC( everyone, i )
			{
				if ( !everyone[i]->IsAlive() )
				{
					everyone[i]->ForceRespawn();
				}
			}
		}
		return;
	}

	if ( m_flNextFloorTime == 0.f )
	{
		m_flNextFloorTime = gpGlobals->curtime + 10.f;
		Announce( "Welcome to the dungeon. No weapons here: MOUSE1 fireball, hold MOUSE2 ka-me-ha-me-ha, R lightning orb, MOUSE3 overheal." );
		return;
	}

	if ( gpGlobals->curtime >= m_flNextFloorTime && party.Count() )
	{
		SpawnFloor( party );
	}
}
