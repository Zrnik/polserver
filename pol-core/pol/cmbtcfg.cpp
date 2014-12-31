/*
History
=======
2005/23/11 MuadDib:   Added warmode_wait timer for changing war mode.
                      Delay default is 1.
2009/09/03 MuadDib:   Moved combat related settings to Combat Config from SSOPT
2009/09/22 Turley:    Added DamagePacket support
2010/01/14 Turley:    Added AttackWhileFrozen

Notes
=======

*/

#include "cmbtcfg.h"
#include "../plib/systemstate.h"

#include "../clib/cfgelem.h"
#include "../clib/cfgfile.h"
#include "../clib/fileutil.h"
#include "../clib/logfacility.h"

#include "globals/uvars.h"

namespace Pol {
  namespace Core {

	void CombatConfig::read_combat_config()
	{
      Clib::ConfigFile cf;
      Clib::ConfigElem elem;
      if ( Clib::FileExists( "config/combat.cfg" ) )
	  {
		cf.open( "config/combat.cfg" );
		cf.readraw( elem );
	  }
	  else if ( Plib::systemstate.config.loglevel > 1 )
		INFO_PRINT << "File config/combat.cfg not found, skipping.\n";

	  gamestate.combat_config.display_parry_success_messages = elem.remove_bool( "DisplayParrySuccessMessages", false );
	  gamestate.combat_config.warmode_inhibits_regen = elem.remove_bool( "WarmodeInhibitsRegen", false );
	  gamestate.combat_config.attack_self = elem.remove_bool( "SingleCombat", false );
	  gamestate.combat_config.warmode_delay = elem.remove_ulong( "WarModeDelay", 1 );
	  gamestate.combat_config.core_hit_sounds = elem.remove_bool( "CoreHitSounds", false );
	  gamestate.combat_config.scripted_attack_checks = elem.remove_bool( "ScriptedAttackChecks", false );
	  gamestate.combat_config.reset_swing_onturn = elem.remove_bool( "ResetSwingOnTurn", false );
	  gamestate.combat_config.send_swing_packet = elem.remove_bool( "SendSwingPacket", true );
	  gamestate.combat_config.send_damage_packet = elem.remove_bool( "SendDamagePacket", false );
	  gamestate.combat_config.attack_while_frozen = elem.remove_bool( "AttackWhileFrozen", true );
	  gamestate.combat_config.send_attack_msg = elem.remove_bool( "SendAttackMsg", true );
	}
  }
}