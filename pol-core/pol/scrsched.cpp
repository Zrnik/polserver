/** @file
 *
 * @par History
 * - 2005/09/16 Shinigami: added scripts_thread_script* to support better debugging
 * - 2006/05/11 Shinigami: better logging in \::signal_event()
 * - 2006/05/27 Shinigami: fixed a smaller cout-bug
 * - 2006/09/17 Shinigami: \::signal_event() will return error on full evene queue
 * - 2006/09/23 Shinigami: Script_Cycles, Sleep_Cycles and Script_passes uses 64bit now
 * - 2009/07/28 MuadDib:   Packet Struct Refactoring
 * - 2009/09/03 MuadDib:   Relocation of boat related cpp/h
 * - 2010/02/04 Turley:    "Event queue full" cerr only if loglevel>=11
 *                         polcfg.discard_old_events discards oldest event if queue is full
 */


#include "scrsched.h"

#include <ctime>
#include <exception>

#include "../bscript/berror.h"
#include "../bscript/bobject.h"
#include "../clib/logfacility.h"
#include "../clib/passert.h"
#include "../clib/refptr.h"
#include "../plib/systemstate.h"
#include "globals/script_internals.h"
#include "globals/state.h"
#include "module/attributemod.h"
#include "module/basiciomod.h"
#include "module/basicmod.h"
#include "module/boatmod.h"
#include "module/cfgmod.h"
#include "module/clmod.h"
#include "module/datastore.h"
#include "module/filemod.h"
#include "module/guildmod.h"
#include "module/httpmod.h"
#include "module/mathmod.h"
#include "module/partymod.h"
#include "module/polsystemmod.h"
#include "module/sqlmod.h"
#include "module/storagemod.h"
#include "module/unimod.h"
#include "module/uomod.h"
#include "module/utilmod.h"
#include "module/vitalmod.h"
#include "polsig.h"
#include "profile.h"
#include "scrdef.h"
#include "scrstore.h"
#include "uoexec.h"
#include <format/format.h>
#include <initializer_list>

namespace Pol
{
namespace Core
{
bool find_uoexec( unsigned int pid, UOExecutor** pp_uoexec )
{
  return scriptScheduler.find_exec( pid, pp_uoexec );
}

void run_ready()
{
  scriptScheduler.run_ready();
}


void check_blocked( polclock_t* pclocksleft )
{
  polclock_t now_clock = polclock();
  stateManager.profilevars.sleep_cycles +=
      scriptScheduler.getHoldlist().size() + scriptScheduler.getNoTimeoutHoldlist().size();
  polclock_t clocksleft = POLCLOCKS_PER_SEC * 60;
  for ( ;; )
  {
    THREAD_CHECKPOINT( scripts, 131 );

    auto itr = scriptScheduler.getHoldlist().cbegin();
    if ( itr == scriptScheduler.getHoldlist().cend() )
      break;

    UOExecutor* ex = ( *itr ).second;
    // ++ex->sleep_cycles;

    passert( ex->blocked() );
    passert( ex->sleep_until_clock() != 0 );
    clocksleft = ex->sleep_until_clock() - now_clock;
    if ( clocksleft <= 0 )
    {
      if ( clocksleft == 0 )
        INC_PROFILEVAR( scripts_ontime );
      else
        INC_PROFILEVAR( scripts_late );
      // wakey-wakey
      // read comment above to understand what goes on here.
      // the return value is already on the stack.
      THREAD_CHECKPOINT( scripts, 132 );
      ex->revive();
    }
    else
    {
      break;
    }
  }
  *pclocksleft = clocksleft;
}

void step_scripts( polclock_t* clocksleft, bool* pactivity )
{
  THREAD_CHECKPOINT( scripts, 102 );
  *pactivity = ( !scriptScheduler.getRunlist().empty() );
  THREAD_CHECKPOINT( scripts, 103 );

  run_ready();

  THREAD_CHECKPOINT( scripts, 104 );

  check_blocked( clocksleft );
  THREAD_CHECKPOINT( scripts, 105 );
  if ( !scriptScheduler.getRunlist().empty() )
    *clocksleft = 0;
  THREAD_CHECKPOINT( scripts, 106 );
}

void add_common_exmods( Core::UOExecutor& ex )
{
  using namespace Module;
  ex.addModule( new BasicExecutorModule( ex ) );
  ex.addModule( new BasicIoExecutorModule( ex ) );
  ex.addModule( new ClilocExecutorModule( ex ) );
  ex.addModule( new MathExecutorModule( ex ) );
  ex.addModule( new UtilExecutorModule( ex ) );
  // ex.addModule( new FileExecutorModule( ex ) );
  ex.addModule( new ConfigFileExecutorModule( ex ) );
  ex.addModule( new UBoatExecutorModule( ex ) );
  ex.addModule( new DataFileExecutorModule( ex ) );
  ex.addModule( new PolSystemExecutorModule( ex ) );
  ex.addModule( new AttributeExecutorModule( ex ) );
  ex.addModule( new VitalExecutorModule( ex ) );
  ex.addModule( new StorageExecutorModule( ex ) );
  ex.addModule( new GuildExecutorModule( ex ) );
  ex.addModule( new UnicodeExecutorModule( ex ) );
  ex.addModule( new PartyExecutorModule( ex ) );
  ex.addModule( new SQLExecutorModule( ex ) );
  ex.addModule( CreateFileAccessExecutorModule( ex ) );
}

bool run_script_to_completion_worker( UOExecutor& ex, Bscript::EScriptProgram* prog )
{
  add_common_exmods( ex );
  ex.addModule( new Module::UOExecutorModule( ex ) );

  ex.setProgram( prog );

  ex.setDebugLevel( Bscript::Executor::NONE );
  ex.set_running_to_completion( true );

  Clib::scripts_thread_script = ex.scriptname();

  if ( Plib::systemstate.config.report_rtc_scripts )
    INFO_PRINT << "Script " << ex.scriptname() << " running..";

  while ( ex.runnable() )
  {
    INFO_PRINT << ".";
    for ( int i = 0; ( i < 1000 ) && ex.runnable(); i++ )
    {
      Clib::scripts_thread_scriptPC = ex.PC;
      ex.execInstr();
    }
  }
  INFO_PRINT << "\n";
  return ( ex.error_ == false );
}

Bscript::BObjectImp* run_executor_to_completion( UOExecutor& ex, const ScriptDef& script )
{
  ref_ptr<Bscript::EScriptProgram> program = find_script2( script );
  if ( program.get() == nullptr )
  {
    ERROR_PRINT << "Error reading script " << script.name() << "\n";
    return new Bscript::BError( "Unable to read script" );
  }

  add_common_exmods( ex );
  ex.addModule( new Module::UOExecutorModule( ex ) );

  ex.setProgram( program.get() );

  ex.setDebugLevel( Bscript::Executor::NONE );
  ex.set_running_to_completion( true );

  Clib::scripts_thread_script = ex.scriptname();

  int i = 0;
  bool reported = false;
  while ( ex.runnable() )
  {
    Clib::scripts_thread_scriptPC = ex.PC;
    ex.execInstr();
    if ( ++i == 1000 )
    {
      if ( reported )
      {
        INFO_PRINT << ".." << ex.PC;
      }
      else
      {
        if ( Plib::systemstate.config.report_rtc_scripts )
        {
          INFO_PRINT << "Script " << script.name() << " running.." << ex.PC;
          reported = true;
        }
      }
      i = 0;
    }
  }
  if ( reported )
    INFO_PRINT << "\n";
  if ( ex.error_ )
    return new Bscript::BError( "Script exited with an error condition" );

  if ( ex.ValueStack.empty() )
    return new Bscript::BLong( 1 );
  else
    return ex.ValueStack.back().get()->impptr()->copy();
}

UOExecutor* create_script_executor()
{
  UOExecutor* ex = new UOExecutor();

  add_common_exmods( *ex );
  // ex->addModule( new UOExecutorModule( *ex ) );
  return ex;
}

UOExecutor* create_full_script_executor()
{
  std::unique_ptr<UOExecutor> ex( new UOExecutor );

  add_common_exmods( *ex );
  ex->addModule( new Module::UOExecutorModule( *ex ) );
  return ex.release();
}

void schedule_executor( UOExecutor* ex )
{
  if ( ex->runnable() )
  {
    scriptScheduler.schedule( ex );
  }
  else
  {
    delete ex;
  }
}

/*
void deschedule_executor( UOExecutor* ex )
{
  for ( ExecList::iterator itr = scriptEngineInternalManager.runlist.begin(),
                           itrend = scriptEngineInternalManager.runlist.end();
        itr != itrend; ++itr )
  {
    if ( *itr == ex )
    {
      scriptEngineInternalManager.runlist.erase( itr );
      break;
    }
  }
  for ( ExecList::iterator itr = scriptEngineInternalManager.ranlist.begin(),
                           itrend = scriptEngineInternalManager.ranlist.end();
        itr != itrend; ++itr )
  {
    if ( *itr == ex )
    {
      scriptEngineInternalManager.ranlist.erase( itr );
      break;
    }
  }
  if ( ex->os_module->blocked_ )
  {
    if ( ex->os_module->in_hold_list_ == Module::OSExecutorModule::TIMEOUT_LIST )
    {
      scriptEngineInternalManager.holdlist.erase( ex->os_module->hold_itr_ );
      ex->os_module->in_hold_list_ = Module::OSExecutorModule::NO_LIST;
    }
    else if ( ex->os_module->in_hold_list_ == Module::OSExecutorModule::NOTIMEOUT_LIST )
    {
      scriptEngineInternalManager.notimeoutholdlist.erase( ex );
      ex->os_module->in_hold_list_ = Module::OSExecutorModule::NO_LIST;
    }
  }
  if ( ex->os_module->in_hold_list_ == Module::OSExecutorModule::DEBUGGER_LIST )
  {
    scriptEngineInternalManager.debuggerholdlist.erase( ex );
    ex->os_module->in_hold_list_ = Module::OSExecutorModule::NO_LIST;
  }
}
*/

void list_script( UOExecutor* uoexec )
{
  fmt::Writer tmp;
  tmp << uoexec->prog_->name.get();
  if ( !uoexec->Globals2.empty() )
    tmp << " Gl=" << uoexec->Globals2.size();
  if ( uoexec->Locals2 && !uoexec->Locals2->empty() )
    tmp << " Lc=" << uoexec->Locals2->size();
  if ( !uoexec->ValueStack.empty() )
    tmp << " VS=" << uoexec->ValueStack.size();
  if ( !uoexec->upperLocals2.empty() )
    tmp << " UL=" << uoexec->upperLocals2.size();
  if ( !uoexec->ControlStack.empty() )
    tmp << " CS=" << uoexec->ControlStack.size();
  INFO_PRINT << tmp.str() << "\n";
}

void list_scripts( const char* desc, const ExecList& ls )
{
  INFO_PRINT << desc << " scripts:\n";
  for ( auto& exec : ls )
  {
    list_script( exec );
  }
}

void list_scripts()
{
  list_scripts( "running", scriptScheduler.getRunlist() );
  // list_scripts( "holding", holdlist );
  list_scripts( "ran", scriptScheduler.getRanlist() );
}

void list_crit_script( UOExecutor* uoexec )
{
  if ( uoexec->critical() )
    list_script( uoexec );
}
void list_crit_scripts( const char* desc, const ExecList& ls )
{
  INFO_PRINT << desc << " scripts:\n";
  for ( auto& exec : ls )
  {
    list_crit_script( exec );
  }
}

void list_crit_scripts()
{
  list_crit_scripts( "running", scriptScheduler.getRunlist() );
  // list_crit_scripts( "holding", holdlist );
  list_crit_scripts( "ran", scriptScheduler.getRanlist() );
}

Bscript::BObjectImp* run_script_to_completion2(
    const ScriptDef& script, const std::initializer_list<Bscript::BObjectImp*>& args )
{
  std::vector<Bscript::BObject> refArgs;
  for ( auto& x : args )
  {
    refArgs.emplace_back( Bscript::BObject( x ) );
  }

  UOExecutor ex;

  std::for_each( refArgs.rbegin(), refArgs.rend(), [&]( auto& x ) { ex.pushArg( x.impptr() ); } );

  return run_executor_to_completion( ex, script );
}

bool run_script_to_completion2( const char* filename,
                                const std::initializer_list<Bscript::BObjectImp*>& args )
{
  std::vector<Bscript::BObject> refArgs;
  for ( auto& x : args )
  {
    refArgs.emplace_back( Bscript::BObject( x ) );
  }

  ref_ptr<Bscript::EScriptProgram> program = find_script( filename );
  if ( program.get() == nullptr )
  {
    ERROR_PRINT << "Error reading script " << filename << "\n";
    return false;
  }

  UOExecutor ex;
  if ( program->haveProgram )
  {
    std::for_each( refArgs.rbegin(), refArgs.rend(), [&]( auto& x ) { ex.pushArg( x.impptr() ); } );
  }
  return run_script_to_completion_worker( ex, program.get() );
}


bool call_script2( const ScriptDef& script,
                   const std::initializer_list<Bscript::BObjectImp*>& args )
{
  try
  {
    Bscript::BObject ob( run_script_to_completion2( script, args ) );
    return ob.isTrue();
  }
  catch ( std::exception& )  //...
  {
    return false;
  }
}


void start_script2( const char* filename, const std::initializer_list<Bscript::BObjectImp*>& args )
{
  // just to delete if it doesn't go somewhere else
  std::vector<Bscript::BObject> refArgs;
  for ( auto& x : args )
  {
    refArgs.emplace_back( Bscript::BObject( x ) );
  }

  ref_ptr<Bscript::EScriptProgram> program = find_script( filename );
  if ( program.get() == nullptr )
  {
    ERROR_PRINT << "Error reading script " << filename << "\n";
    throw std::runtime_error( "Error starting script" );
  }

  UOExecutor* ex = create_script_executor();
  if ( program->haveProgram )
  {
    std::for_each( refArgs.rbegin(), refArgs.rend(),
                   [&]( auto& x ) { ex->pushArg( x.impptr() ); } );
  }
  // ex->addModule( new FileExecutorModule( *ex ) );
  ex->addModule( new Module::UOExecutorModule( *ex ) );

  if ( !ex->setProgram( program.get() ) )
    throw std::runtime_error( "Error starting script." );

  scriptScheduler.schedule( ex );
}

// EXACTLY the same as start_script, except uses find_script2
Module::UOExecutorModule* start_script2( const ScriptDef& script,
                                         const std::initializer_list<Bscript::BObjectImp*>& args )
{
  // just to delete if it doesn't go somewhere else
  std::vector<Bscript::BObject> refArgs;
  for ( auto& x : args )
  {
    refArgs.emplace_back( Bscript::BObject( x ) );
  }

  ref_ptr<Bscript::EScriptProgram> program = find_script2( script );
  if ( program.get() == nullptr )
  {
    ERROR_PRINT << "Error reading script " << script.name() << "\n";
    // throw runtime_error( "Error starting script" );
    return nullptr;
  }

  std::unique_ptr<UOExecutor> ex( create_script_executor() );
  if ( program->haveProgram )
  {
    std::for_each( refArgs.rbegin(), refArgs.rend(),
                   [&]( auto& x ) { ex->pushArg( x.impptr() ); } );
  }
  // ex->addModule( new FileExecutorModule( *ex ) );
  Module::UOExecutorModule* uoemod = new Module::UOExecutorModule( *ex );
  ex->addModule( uoemod );

  if ( !ex->setProgram( program.get() ) )
  {
    return nullptr;
    // throw runtime_error( "Error starting script." );
  }

  scriptScheduler.schedule( ex.release() );

  return uoemod;
}

// EXACTLY the same as start_script, except uses find_script2
Module::UOExecutorModule* start_script2( ref_ptr<Bscript::EScriptProgram> program,
                                         const std::initializer_list<Bscript::BObjectImp*>& args )
{
  // just to delete if it doesn't go somewhere else
  std::vector<Bscript::BObject> refArgs;
  for ( auto& x : args )
  {
    refArgs.emplace_back( Bscript::BObject( x ) );
  }

  UOExecutor* ex = create_script_executor();
  if ( program->haveProgram )
  {
    std::for_each( refArgs.rbegin(), refArgs.rend(),
                   [&]( auto& x ) { ex->pushArg( x.impptr() ); } );
  }
  // ex->addModule( new FileExecutorModule( *ex ) );
  Module::UOExecutorModule* uoemod = new Module::UOExecutorModule( *ex );
  ex->addModule( uoemod );

  if ( !ex->setProgram( program.get() ) )
    throw std::runtime_error( "Error starting script." );

  scriptScheduler.schedule( ex );

  return uoemod;
}

}  // namespace Core
}  // namespace Pol
