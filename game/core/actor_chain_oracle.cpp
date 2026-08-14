// Observation-only groundwork for the 0x800521C0 -> 0x8001F158 -> 0x8001F798 actor chain.
#include "spyro_game.h"
#include "cfg.h"
#include "core.h"
#include "recomp_iface.h"
#include "rec_decls.h"
#include <lucent/log.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <string_view>
#include <vector>

int gpu_vk_wide_engine(Core*);
namespace {
constexpr uint32_t kRecordBase=0x800712F4u; // 0x8006FCF4 + 5632
constexpr uint32_t kRecordSize=56u;
constexpr uint32_t kDurableRecords=53u;     // source indices 0..52 only
constexpr uint32_t kTerminatorIndex=53u;    // exact terminator observed at 0x80071E8C
constexpr uint32_t kPoolPtr=0x800757B0u;

enum class Family:uint8_t { G4,GT4,G3,GT3,FT4,Unsupported };
struct PacketKey { uint32_t packet=0,record=0;Family family=Family::Unsupported; };

struct ActorRecordRecipe { std::array<uint32_t,14> words{}; };
struct PacketCensus {
  uint32_t packets=0,bytes=0,f3=0,g3=0,ft3=0,gt3=0,f4=0,g4=0,ft4=0,gt4=0,semi=0,raw=0,other=0;
  const char* first="none";
  std::vector<PacketKey> entries;
};
struct CheckpointCensus {
  uint32_t insertions=0,g4=0,gt4=0,g3=0,gt3=0,ft4=0,recordJoins=0,
    badRecord=0,badPacket=0,postSplice=0,finals=0,firstRecord=0,minRecord=0xFFFFFFFFu,maxRecord=0;
  std::vector<PacketKey> entries;
};

void actor_checkpoint(Core* c,uint64_t,uint32_t pc,void* user){
  auto& o=*static_cast<CheckpointCensus*>(user);
  if(pc==0x80020860u){++o.postSplice;return;}if(pc==0x800208ACu){++o.finals;return;}
  Family family=Family::Unsupported;
  if(pc==0x800201A8u){family=Family::G4;++o.g4;}else if(pc==0x8002023Cu){family=Family::GT4;++o.gt4;}
  else if(pc==0x80020430u){family=Family::G3;++o.g3;}else if(pc==0x8002051Cu){family=Family::GT3;++o.gt3;}
  else if(pc==0x8002066Cu){family=Family::FT4;++o.ft4;}else return;
  ++o.insertions;
  // 1F798 saves the 0x38 record cursor in LO before reusing r28 as the material/scratch base.
  const uint32_t record=c->lo,packet=c->r[24],pool=c->mem_r32(kPoolPtr);
  if(!o.firstRecord)o.firstRecord=record;o.minRecord=std::min(o.minRecord,record);o.maxRecord=std::max(o.maxRecord,record);
  if(record>=kRecordBase&&record<kRecordBase+kDurableRecords*kRecordSize&&((record-kRecordBase)%kRecordSize)==0)++o.recordJoins;
  else ++o.badRecord;
  if(packet<pool||packet>=0x80200000u)++o.badPacket;
  o.entries.push_back({packet,record,family});
}

static Family command_family(uint8_t cmd){switch(cmd&0xFCu){case 0x38:return Family::G4;
  case 0x3C:return Family::GT4;case 0x30:return Family::G3;case 0x34:return Family::GT3;
  case 0x2C:return Family::FT4;default:return Family::Unsupported;}}
static bool compare_ordered(const std::vector<PacketKey>& expected,const std::vector<PacketKey>& actual){
  if(expected.size()!=actual.size())return false;
  for(size_t i=0;i<expected.size();++i)if(expected[i].packet!=actual[i].packet||expected[i].family!=actual[i].family)return false;
  return true;
}

template<class Read> bool parse_packets(uint32_t begin,uint32_t end,Read read,PacketCensus& out){
  if(end<begin||((end-begin)&3u)){out.first="span";return false;}
  uint32_t p=begin;
  while(p<end){
    if(end-p<8u){out.first="header";return false;}
    const uint32_t tag=read(p),words=tag>>24,bytes=(words+1u)*4u;
    if(words==0u||bytes>end-p){out.first="stride";return false;}
    const uint8_t cmd=(uint8_t)(read(p+4u)>>24);
    out.semi+=(cmd&2u)!=0u;out.raw+=(cmd&1u)!=0u;
    switch(cmd&0xFCu){
      case 0x20: ++out.f3; break; case 0x30: ++out.g3; break;
      case 0x24: ++out.ft3; break; case 0x34: ++out.gt3; break;
      case 0x28: ++out.f4; break; case 0x38: ++out.g4; break;
      case 0x2C: ++out.ft4; break; case 0x3C: ++out.gt4; break;
      default: ++out.other;out.first="command";return false;
    }
    out.entries.push_back({p,0,command_family(cmd)});
    ++out.packets;out.bytes+=bytes;p+=bytes;
  }
  return p==end;
}

void actor_chain_oracle(Core* c){
  if(gpu_vk_wide_engine(c)){lucent::error("actorchainoracle","REFUSED: diagnostic override would displace active 0x8001F798 widescreen hook");abort();}
  std::vector<ActorRecordRecipe> records;records.reserve(kDurableRecords);
  bool terminated=false;
  for(uint32_t i=0;i<=kTerminatorIndex;++i){const uint32_t p=kRecordBase+i*kRecordSize;
    if(c->mem_r32(p)==0u){terminated=true;break;}ActorRecordRecipe r{};
    if(i==kTerminatorIndex)break;
    for(uint32_t w=0;w<14;++w)r.words[w]=c->mem_r32(p+w*4u);records.push_back(r);}
  const uint32_t before=c->mem_r32(kPoolPtr);
  // PcObserver is deliberately bounded to eight runtime targets.  The five packet-family command
  // sites fire once per emitted packet even when the target OT slot was empty (the later splice
  // write sites do not), and post-splice/final close the lifecycle.
  static constexpr uint32_t targets[]={0x800201A8u,0x8002023Cu,0x80020430u,0x8002051Cu,
    0x8002066Cu,0x80020860u,0x800208ACu};CheckpointCensus checkpoints{};
  if(!c->pcObserver.arm(targets,std::size(targets),actor_checkpoint,&checkpoints))abort();
  gen_func_8001F798(c);
  const uint64_t seen=c->pcObserver.seen(),matched=c->pcObserver.matched();c->pcObserver.disarm();
  const uint32_t after=c->mem_r32(kPoolPtr);PacketCensus census{};
  const bool parsed=parse_packets(before,after,[&](uint32_t p){return c->mem_r32(p);},census);
  bool ordered=compare_ordered(checkpoints.entries,census.entries);
  const char* orderedFirst="none";
  if(!ordered)orderedFirst="address_or_family";
  const bool families=checkpoints.g4==census.g4&&checkpoints.gt4==census.gt4&&checkpoints.g3==census.g3&&
    checkpoints.gt3==census.gt3&&checkpoints.ft4==census.ft4&&census.f3==0&&census.ft3==0&&census.f4==0&&census.other==0;
  const bool positive=terminated&&parsed&&ordered&&families&&
    checkpoints.insertions==census.packets&&checkpoints.insertions==checkpoints.recordJoins&&checkpoints.badRecord==0&&checkpoints.badPacket==0&&
    checkpoints.postSplice>0&&checkpoints.finals==1;
  bool negative=false;
  if(after>before){PacketCensus corrupt{};negative=!parse_packets(before,after,[&](uint32_t p){
      uint32_t v=c->mem_r32(p);if(p==before+4u)v=(v&0x00FFFFFFu)|0x7C000000u;return v;},corrupt)&&
      corrupt.first==std::string_view("command");}
  const char* result=!terminated?"NO_TERMINATOR":after==before?"NO_PACKETS":positive&&negative?"PASS":"FAIL";
  lucent::info("actorchainoracle","records={}/{} terminated={} checkpoints={}/{} insertions={} joins={} ordered={} ordered_first={} bad_record={} record_range={:08X}..{:08X} first_record={:08X} bad_packet={} families[G4={} GT4={} G3={} GT3={} FT4={}] post/final={}/{} packets={} bytes={} F3={} G3={} FT3={} GT3={} F4={} G4={} FT4={} GT4={} semi={} raw={} other={} corrupt_rejected={} first={} result={}",
    records.size(),kDurableRecords,terminated,seen,matched,checkpoints.insertions,checkpoints.recordJoins,ordered,orderedFirst,
    checkpoints.badRecord,checkpoints.minRecord,checkpoints.maxRecord,checkpoints.firstRecord,checkpoints.badPacket,checkpoints.g4,checkpoints.gt4,checkpoints.g3,checkpoints.gt3,checkpoints.ft4,
    checkpoints.postSplice,checkpoints.finals,census.packets,census.bytes,census.f3,census.g3,census.ft3,census.gt3,
    census.f4,census.g4,census.ft4,census.gt4,census.semi,census.raw,census.other,negative,census.first,result);
  // Groundwork is deliberately observation-only: a failed join is the diagnostic result, not a
  // reason to crash an otherwise valid generated render.  Promotion to an acceptance oracle will
  // make a nonempty mismatch fatal only after source/payload/bin joins are independently green.
}
}

void spyro_register_actor_chain_oracle(){
  if(!cfg_on("PSXPORT_ACTOR_CHAIN_ORACLE"))return;
  if(cfg_on("PSXPORT_NDIFF_IDENTITY")){lucent::error("actorchainoracle","REFUSED: PSXPORT_NDIFF_IDENTITY can overwrite the 0x8001F798 diagnostic override");abort();}
  if(const char* trace=cfg_str("PSXPORT_FNTRACE");trace&&(std::string_view(trace).find("1F798")!=std::string_view::npos||std::string_view(trace).find("1f798")!=std::string_view::npos)){
    lucent::error("actorchainoracle","REFUSED: PSXPORT_FNTRACE targets the same 0x8001F798 override slot");abort();}
  lucent::info("actorchainoracle","armed 0x8001F798; 0x800521C0 list partition and 0x8001F158 record builder remain generated ground truth");
  psxport_recomp()->shard_set_override(0x8001F798u,actor_chain_oracle);
}

int spyro_actor_chain_oracle_selftest(){
  constexpr uint32_t base=0x1000u;std::array<uint32_t,49> w{};
  w[0]=0x08001024u;w[1]=0x3B000000u;   // G4, semi+raw, 9 words
  w[9]=0x0C000000u;w[10]=0x3C000000u; // GT4, 13 words
  w[22]=0x06000000u;w[23]=0x30000000u;// G3, 7 words
  w[29]=0x09000000u;w[30]=0x34000000u;// GT3, 10 words
  w[39]=0x09000000u;w[40]=0x2C000000u;// FT4, 10 words
  PacketCensus good{};bool ok=parse_packets(base,base+196u,[&](uint32_t p){return w[(p-base)/4u];},good)&&
    good.packets==5u&&good.g4==1u&&good.gt4==1u&&good.g3==1u&&good.gt3==1u&&good.ft4==1u&&
    good.semi==1u&&good.raw==1u&&good.bytes==196u;
  std::vector<PacketKey> expected=good.entries,corruptAddress=expected,corruptFamily=expected;
  corruptAddress[0].packet+=4u;corruptFamily[0].family=Family::GT4;
  ok=ok&&compare_ordered(expected,good.entries)&&!compare_ordered(expected,corruptAddress)&&!compare_ordered(expected,corruptFamily);
  PacketCensus bad{};w[1]=0x7C000000u;ok=ok&&!parse_packets(base,base+196u,[&](uint32_t p){return w[(p-base)/4u];},bad)&&bad.other==1u;
  lucent::info("selftest","{}(actorchainrecipe): packets={} bytes={} G4={} GT4={} G3={} GT3={} FT4={} semi={} raw={} corrupt_address=1 corrupt_family=1 corrupt_command={}",ok?"PASS":"FAIL",good.packets,good.bytes,good.g4,good.gt4,good.g3,good.gt3,good.ft4,good.semi,good.raw,bad.other==1u);
  return ok?0:1;
}
