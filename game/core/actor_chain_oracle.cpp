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
struct SourceSnapshot {
  uint32_t record=0,source=0,r1=0,aux=0,scratch=0,depthOrigin=0,shift=0;
  uint32_t depthBase=0,colorBase=0,fog=0,pool=0,localOt=0;
  std::array<uint32_t,10> words{};
  std::array<uint32_t,4> xy{},depth{},color{};
};
static bool durable_record(uint32_t p){return p>=kRecordBase&&p<kRecordBase+kDurableRecords*kRecordSize&&((p-kRecordBase)%kRecordSize)==0;}
static bool ram_word_span(uint32_t p,uint32_t bytes){
  return (p&3u)==0u&&p>=0x80000000u&&bytes>=4u&&p<=0x801FFFFFu-(bytes-1u);
}
static bool scratch_word(uint32_t p){return (p&3u)==0u&&p>=0x1F800000u&&p<=0x1F8003FCu;}
static bool guest_word(uint32_t p){return scratch_word(p)||ram_word_span(p,4u);}
template<class Read> static bool capture_source(Read read,uint32_t record,uint32_t source,uint32_t auxAddr,
    uint32_t r1,uint32_t scratch,uint32_t depthOrigin,uint32_t shift,SourceSnapshot& out){
  if(!durable_record(record)||!ram_word_span(source,40u)||!ram_word_span(auxAddr,4u))return false;
  out={};out.record=record;out.source=source;out.r1=r1;out.scratch=scratch;
  out.depthOrigin=depthOrigin;out.shift=shift;out.aux=read(auxAddr);
  for(uint32_t i=0;i<out.words.size();++i)out.words[i]=read(source+i*4u);
  return true;
}

struct ActorRecordRecipe { std::array<uint32_t,14> words{}; };
struct PacketCensus {
  uint32_t packets=0,bytes=0,f3=0,g3=0,ft3=0,gt3=0,f4=0,g4=0,ft4=0,gt4=0,semi=0,raw=0,other=0;
  const char* first="none";
  std::vector<PacketKey> entries;
};
struct EpochState {bool active=false,bSeen=false,familySeen=false;uint32_t source=0,record=0;};
static void epoch_clear(EpochState& e){e={};}
static void epoch_open(EpochState& e,uint32_t source,uint32_t record){e={true,false,false,source,record};}
static bool epoch_subset(EpochState& e,uint32_t source,uint32_t record){
  if(!e.active||e.bSeen||e.source!=source||e.record!=record)return false;e.bSeen=true;return true;
}
static bool epoch_family(EpochState& e,uint32_t cursor,uint32_t record,uint32_t expectedCursor){
  const bool ok=e.active&&!e.familySeen&&e.record==record&&cursor==expectedCursor;e.familySeen=true;e.active=false;return ok;
}
struct CheckpointCensus {
  uint32_t insertions=0,g4=0,gt4=0,g3=0,gt3=0,ft4=0,recordJoins=0,
    badRecord=0,badPacket=0,postSplice=0,finals=0,firstRecord=0,minRecord=0xFFFFFFFFu,maxRecord=0,
    sourceA=0,sourceB=0,badSource=0,badSourceRecord=0,badClassifier=0,badTables=0,
    payloadCompared=0,payloadMismatch=0,directTri=0,quadFirst=0,quadSecond=0,unsupportedPayload=0;
  uint32_t firstBadTable=0;
  EpochState epoch{};
  std::vector<SourceSnapshot> sources;
  std::vector<PacketKey> entries;
  struct Expected { uint32_t packet=0;Family family=Family::Unsupported;std::vector<uint32_t> words; };
  std::vector<Expected> expected;
};

static uint32_t sar(uint32_t v,uint32_t n){return (uint32_t)((int32_t)v>>(n&31u));}
static bool capture_tables(Core* c,SourceSnapshot& s,uint32_t& badAddr){
  const uint32_t w0=s.words[0],w1=s.words[1],w2=s.words[2];
  const uint32_t vo[]={ (w0>>20)&0x7FCu,(w0>>11)&0x7FCu,(w0>>2)&0x7FCu,w2&0x7FCu };
  const uint32_t co[]={ (w1>>17)&0x7FCu,(w1>>8)&0x7FCu,(w1<<1)&0x7FCu,(w2>>9)&0x7FCu };
  const unsigned count=(int32_t)w0<0?4u:3u;
  for(unsigned i=0;i<count;++i){const uint32_t xp=s.scratch+vo[i],zp=s.depthBase+vo[i],cp=s.colorBase+co[i];
    if(!scratch_word(xp)){badAddr=xp;return false;}if(!guest_word(zp)){badAddr=zp;return false;}
    if(!ram_word_span(cp,4u)){badAddr=cp;return false;}}
  for(unsigned i=0;i<count;++i){uint32_t xy=c->mem_r32(s.scratch+vo[i]);s.xy[i]=(int32_t(s.shift)<0)?sar(xy,5):xy;
    s.depth[i]=c->mem_r32(s.depthBase+vo[i]);s.color[i]=c->mem_r32(s.colorBase+co[i]);}
  s.color[0]&=0x00FFFFFFu;return true;
}

static std::vector<uint32_t> expected_payload(const SourceSnapshot& s,Family f,bool quadSecond){
  const uint32_t semi=(s.words[1]&1u)<<25;
  switch(f){
    case Family::G4:return {0x08000000u,s.color[0]+0x38000000u,s.xy[0],s.color[1],s.xy[1],s.color[2],s.xy[2],s.color[3],s.xy[3]};
    case Family::GT4:return {0x0C000000u,s.color[0]+0x3C000000u+semi,s.xy[0],s.words[3]+s.fog,s.color[1],s.xy[1],s.words[4],s.color[2],s.xy[2],s.words[5],s.color[3],s.xy[3],s.words[5]>>16};
    case Family::G3:if(quadSecond)return {0x06000000u,(s.color[3]&0x00FFFFFFu)+0x30000000u,s.xy[3],s.color[1],s.xy[1],s.color[2],s.xy[2]};
      else return {0x06000000u,s.color[0]+0x30000000u,s.xy[0],s.color[1],s.xy[1],s.color[2],s.xy[2]};
    case Family::GT3:{const bool quad=(int32_t)s.words[0]<0;uint32_t uv0=(quad?s.words[3]:s.words[2])+s.fog;
      const uint32_t uv1=quad?s.words[4]:s.words[3],uv2=quad?s.words[5]:s.words[4];
      if(quadSecond)uv0=(uv0&0xFFFF0000u)|(s.words[5]>>16);
      const unsigned a=quadSecond?3u:0u;
      return {0x09000000u,(s.color[a]&0x00FFFFFFu)+0x34000000u+semi,s.xy[a],uv0,s.color[1],s.xy[1],uv1,s.color[2],s.xy[2],uv2};}
    default:return {};
  }
}
struct PayloadCompare {uint32_t compared=0,mismatches=0,expected=0,actual=0,packet=0,index=0;const char* first="none";};
template<class Read> static PayloadCompare compare_payloads(const std::vector<CheckpointCensus::Expected>& expected,
    uint32_t poolBegin,uint32_t poolEnd,Read read){
  PayloadCompare out{};
  for(const auto& e:expected){
    const uint64_t end=(uint64_t)e.packet+e.words.size()*4u;
    if(e.words.empty()||e.packet<poolBegin||end>poolEnd){++out.mismatches;if(out.first==std::string_view("none"))out.first="span";continue;}
    for(size_t i=0;i<e.words.size();++i){const uint32_t actual=read(e.packet+(uint32_t)i*4u);
      const bool same=i==0?(actual&0xFF000000u)==e.words[i]:actual==e.words[i];
      if(!same){++out.mismatches;if(out.first==std::string_view("none")){out.first=i==0?"tag":i==1?"command_color":i%2==0?"xy":"color_uv";
          out.expected=e.words[i];out.actual=actual;out.packet=e.packet;out.index=(uint32_t)i;}break;}}
    ++out.compared;
  }
  return out;
}

struct OtEntry {uint32_t packet=0,bin=0;};
struct OtCensus {
  EpochState epoch{};SourceSnapshot source{};bool haveSource=false;
  uint32_t candidates=0,emitted=0,pre=0,post=0,finals=0,binsScanned=0,nonempty=0,nodes=0,
    badSource=0,badEpoch=0,badBin=0,cycles=0,duplicates=0,outOfRange=0,emptyAppend=0,nonemptyAppend=0;
  std::vector<OtEntry> expected,actual;
};
static uint32_t expected_bin(const SourceSnapshot& s,Family f,bool second){
  const bool quad=f==Family::G4||f==Family::GT4;
  uint32_t d=0;
  if(quad)d=s.depth[0]-s.depthOrigin+s.depth[1]+s.depth[2]+s.depth[3];
  else {const unsigned a=second?3u:0u;d=s.depth[a]+(s.depth[a]>>1)-s.depthOrigin+s.depth[1]+(s.depth[1]>>1)+s.depth[2];}
  const uint32_t bias=(uint32_t)((int32_t)s.words[1]>>28)<<1;
  uint32_t q;
  if(quad)q=sar(d+(bias<<(s.shift&31u)),s.shift);else q=sar(d,s.shift)+bias;
  return s.localOt+(q<<3);
}
static bool compare_ot(const std::vector<OtEntry>& a,const std::vector<OtEntry>& b,uint32_t& first,const char*& field){
  if(a.size()!=b.size()){first=(uint32_t)std::min(a.size(),b.size());field="count";return false;}
  for(size_t i=0;i<a.size();++i){if(a[i].bin!=b[i].bin){first=(uint32_t)i;field="bin";return false;}
    if(a[i].packet!=b[i].packet){first=(uint32_t)i;field="fifo";return false;}}
  first=0;field="none";return true;
}
template<class Read> static void snapshot_ot_read(OtCensus& o,Read read){
  if(!o.haveSource)return;
  const uint32_t base=o.source.localOt,startNonempty=o.nonempty;std::vector<uint32_t> seen;
  for(uint32_t bin=0;bin<288u;++bin){++o.binsScanned;const uint32_t newest=read(base+bin*8u),oldest=read(base+bin*8u+4u);
    if(!newest&&!oldest)continue;++o.nonempty;if(!newest||!oldest){++o.outOfRange;continue;}
    uint32_t p=oldest;bool reached=false;
    for(uint32_t guard=0;guard<o.expected.size()+1u;++guard){
      if(p<0x80000000u||p>=0x80200000u){++o.outOfRange;break;}
      if(std::find(seen.begin(),seen.end(),p)!=seen.end()){++o.duplicates;break;}seen.push_back(p);
      o.actual.push_back({p,bin});++o.nodes;if(p==newest){reached=true;break;}
      const uint32_t next=0x80000000u|(read(p)&0x00FFFFFFu);if(next==p){++o.cycles;break;}p=next;
    }
    if(!reached)++o.cycles;
  }
  if(o.nonempty!=startNonempty)for(const auto& e:o.expected){unsigned prior=0;for(const auto& q:o.expected){if(&q==&e)break;if(q.bin==e.bin)++prior;}
      if(prior)++o.nonemptyAppend;else ++o.emptyAppend;}
}
static void snapshot_ot(Core* c,OtCensus& o){snapshot_ot_read(o,[&](uint32_t p){return c->mem_r32(p);});}
static void actor_ot_checkpoint(Core* c,uint64_t,uint32_t pc,void* user){
  auto& o=*static_cast<OtCensus*>(user);
  if(pc==0x8001FFF8u){epoch_clear(o.epoch);o.haveSource=false;++o.candidates;SourceSnapshot s{};
    const uint32_t record=c->lo,source=c->r[30];
    if(!capture_source([&](uint32_t p){return c->mem_r32(p);},record,source,source+4u,c->r[1],c->r[29],c->r[22],c->r[23],s)){++o.badSource;return;}
    s.depthBase=c->r[28];s.colorBase=c->r[25];s.fog=c->r[18];s.pool=c->r[24];s.localOt=c->r[19];uint32_t bad=0;
    if(!capture_tables(c,s,bad)){++o.badSource;return;}o.source=s;o.haveSource=true;epoch_open(o.epoch,source,record);return;}
  if(pc==0x8002074Cu){++o.pre;snapshot_ot(c,o);return;}if(pc==0x80020860u){++o.post;return;}if(pc==0x800208ACu){++o.finals;return;}
  Family f=Family::Unsupported;if(pc==0x800201A8u)f=Family::G4;else if(pc==0x8002023Cu)f=Family::GT4;
  else if(pc==0x80020430u)f=Family::G3;else if(pc==0x8002051Cu)f=Family::GT3;else return;
  ++o.emitted;if(!o.haveSource){++o.badEpoch;return;}const bool quad=(int32_t)o.source.words[0]<0;
  uint32_t cursor=o.epoch.source+((f==Family::G4||f==Family::G3)?(quad?12u:8u):(quad?24u:20u));
  if(!epoch_family(o.epoch,c->r[30],c->lo,cursor)){++o.badEpoch;return;}
  const bool second=(f==Family::G3||f==Family::GT3)&&quad&&(int32_t)c->r[17]>0;
  const uint32_t addr=expected_bin(o.source,f,second);if(addr<o.source.localOt||((addr-o.source.localOt)&7u)||
      (addr-o.source.localOt)/8u>=288u){++o.badBin;return;}
  o.expected.push_back({c->r[24],(addr-o.source.localOt)/8u});
}

static void actor_chain_ot_oracle(Core* c){
  if(gpu_vk_wide_engine(c)){lucent::error("actorchainoracle","REFUSED: OT diagnostic conflicts with widescreen 0x8001F798 hook");abort();}
  static constexpr uint32_t targets[]={0x8001FFF8u,0x800201A8u,0x8002023Cu,0x80020430u,0x8002051Cu,0x8002074Cu,0x80020860u,0x800208ACu};
  OtCensus o{};if(!c->pcObserver.arm(targets,std::size(targets),actor_ot_checkpoint,&o))abort();gen_func_8001F798(c);
  const uint64_t seen=c->pcObserver.seen(),matched=c->pcObserver.matched();c->pcObserver.disarm();
  std::stable_sort(o.expected.begin(),o.expected.end(),[](const OtEntry& a,const OtEntry& b){return a.bin<b.bin;});uint32_t first=0;const char* field="none";
  const uint64_t expectedMatched=(uint64_t)o.candidates+o.emitted+o.pre+o.post+o.finals;
  const bool ordered=compare_ot(o.expected,o.actual,first,field);const bool positive=o.candidates>0&&o.emitted>0&&matched==expectedMatched&&o.pre>0&&o.pre==o.post&&o.finals==1&&
    o.binsScanned==288u*o.pre&&o.nonempty>0&&o.emptyAppend>0&&o.nonemptyAppend>0&&o.expected.size()==o.emitted&&o.nodes==o.emitted&&
    o.badSource==0&&o.badEpoch==0&&o.badBin==0&&o.cycles==0&&o.duplicates==0&&o.outOfRange==0&&ordered;
  lucent::info("actorchainoracle","pass=ot checkpoints={}/{} candidates={} emitted={} expected={} actual={} bins_scanned={} nonempty={} append[empty={} nonempty={}] pre/post/final={}/{}/{} bad_source={} bad_epoch={} bad_bin={} cycles={} duplicates={} out_of_range={} ordered={} first={} field={} result={}",seen,matched,o.candidates,o.emitted,o.expected.size(),o.actual.size(),o.binsScanned,o.nonempty,o.emptyAppend,o.nonemptyAppend,o.pre,o.post,o.finals,o.badSource,o.badEpoch,o.badBin,o.cycles,o.duplicates,o.outOfRange,ordered,first,field,positive?"PASS":"FAIL");
}

void actor_checkpoint(Core* c,uint64_t,uint32_t pc,void* user){
  auto& o=*static_cast<CheckpointCensus*>(user);
  if(pc==0x8002031Cu){
    ++o.sourceB;if(!epoch_subset(o.epoch,c->r[30],c->lo))++o.badClassifier;return;
  }
  if(pc==0x8001FFF8u){
    epoch_clear(o.epoch);
    SourceSnapshot s{};const uint32_t record=c->lo,source=c->r[30],auxAddr=c->r[30]+4u;
    if(!capture_source([&](uint32_t p){return c->mem_r32(p);},record,source,auxAddr,
        c->r[1],c->r[29],c->r[22],c->r[23],s)){
      ++o.badSource;if(!durable_record(record))++o.badSourceRecord;
    }else{s.depthBase=c->r[28];s.colorBase=c->r[25];s.fog=c->r[18];s.pool=c->r[24];s.localOt=c->r[19];
      uint32_t badAddr=0;if(!capture_tables(c,s,badAddr)){++o.badSource;++o.badTables;if(!o.firstBadTable)o.firstBadTable=badAddr;}
      else{o.sources.push_back(s);epoch_open(o.epoch,source,record);}}
    ++o.sourceA;return;
  }
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
  if(o.sources.empty()){++o.unsupportedPayload;return;}
  const bool quad=(int32_t)o.sources.back().words[0]<0;
  uint32_t expectedCursor=o.epoch.source;
  if(family==Family::G4||family==Family::G3)expectedCursor+=quad?12u:8u;
  else if(family==Family::GT4||family==Family::GT3)expectedCursor+=quad?24u:20u;
  if(!epoch_family(o.epoch,c->r[30],record,expectedCursor)){++o.badClassifier;++o.unsupportedPayload;return;}
  const bool second=(family==Family::G3||family==Family::GT3)&&quad&&(int32_t)c->r[17]>0;
  if(family==Family::G3||family==Family::GT3){if(!quad)++o.directTri;else if(second)++o.quadSecond;else ++o.quadFirst;}
  auto words=expected_payload(o.sources.back(),family,second);
  if(words.empty())++o.unsupportedPayload;else o.expected.push_back({packet,family,std::move(words)});
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
  // Payload/source pass: two source heads + five family sites + final = PcObserver's exact limit.
  static constexpr uint32_t targets[]={0x8001FFF8u,0x8002031Cu,0x800201A8u,0x8002023Cu,
    0x80020430u,0x8002051Cu,0x8002066Cu,0x800208ACu};CheckpointCensus checkpoints{};
  if(!c->pcObserver.arm(targets,std::size(targets),actor_checkpoint,&checkpoints))abort();
  gen_func_8001F798(c);
  const uint64_t seen=c->pcObserver.seen(),matched=c->pcObserver.matched();c->pcObserver.disarm();
  const uint32_t after=c->mem_r32(kPoolPtr);PacketCensus census{};
  const bool parsed=parse_packets(before,after,[&](uint32_t p){return c->mem_r32(p);},census);
  const PayloadCompare payload=compare_payloads(checkpoints.expected,before,after,[&](uint32_t p){return c->mem_r32(p);});
  bool ordered=compare_ordered(checkpoints.entries,census.entries);
  const char* orderedFirst="none";
  if(!ordered)orderedFirst="address_or_family";
  const bool families=checkpoints.g4==census.g4&&checkpoints.gt4==census.gt4&&checkpoints.g3==census.g3&&
    checkpoints.gt3==census.gt3&&checkpoints.ft4==census.ft4&&census.f3==0&&census.ft3==0&&census.f4==0&&census.other==0;
  const uint64_t expectedMatched=(uint64_t)checkpoints.sourceA+checkpoints.sourceB+checkpoints.insertions+checkpoints.finals;
  const bool positive=terminated&&parsed&&ordered&&families&&
    checkpoints.insertions==census.packets&&checkpoints.insertions==checkpoints.recordJoins&&checkpoints.badRecord==0&&checkpoints.badPacket==0&&
    checkpoints.sourceA>0&&checkpoints.sourceB>0&&!checkpoints.sources.empty()&&checkpoints.badSource==0&&checkpoints.badClassifier==0&&
    checkpoints.badSourceRecord==0&&checkpoints.badTables==0&&checkpoints.finals==1&&matched==expectedMatched&&
    checkpoints.unsupportedPayload==0&&payload.compared==census.packets&&payload.mismatches==0;
  bool negative=false;
  if(after>before){PacketCensus corrupt{};negative=!parse_packets(before,after,[&](uint32_t p){
      uint32_t v=c->mem_r32(p);if(p==before+4u)v=(v&0x00FFFFFFu)|0x7C000000u;return v;},corrupt)&&
      corrupt.first==std::string_view("command");}
  const char* result=!terminated?"NO_TERMINATOR":after==before?"NO_PACKETS":positive&&negative?"PASS":"FAIL";
  lucent::info("actorchainoracle","pass=payload records={}/{} terminated={} checkpoints={}/{} candidates={} positive_subset={} emitted={} candidate_minus_packets={} bad_source={} bad_source_record={} bad_classifier={} bad_tables={} first_bad_table={:08X} joins={} ordered={} ordered_first={} payload={}/{} payload_mismatch={} payload_first={} payload_witness[p={:08X} i={} exp={:08X} act={:08X}] unsupported_payload={} origin[direct={} quad_first={} quad_second={}] bad_record={} record_range={:08X}..{:08X} first_record={:08X} bad_packet={} families[G4={} GT4={} G3={} GT3={} FT4={}] final={} packets={} bytes={} F3={} G3={} FT3={} GT3={} F4={} G4={} FT4={} GT4={} semi={} raw={} other={} corrupt_rejected={} first={} result={}",
    records.size(),kDurableRecords,terminated,seen,matched,checkpoints.sourceA,checkpoints.sourceB,checkpoints.insertions,
    (int64_t)checkpoints.sourceA-(int64_t)checkpoints.insertions,checkpoints.badSource,checkpoints.badSourceRecord,checkpoints.badClassifier,checkpoints.badTables,checkpoints.firstBadTable,checkpoints.recordJoins,ordered,orderedFirst,
    payload.compared,census.packets,payload.mismatches,payload.first,payload.packet,payload.index,payload.expected,payload.actual,checkpoints.unsupportedPayload,checkpoints.directTri,checkpoints.quadFirst,checkpoints.quadSecond,
    checkpoints.badRecord,checkpoints.minRecord,checkpoints.maxRecord,checkpoints.firstRecord,checkpoints.badPacket,checkpoints.g4,checkpoints.gt4,checkpoints.g3,checkpoints.gt3,checkpoints.ft4,
    checkpoints.finals,census.packets,census.bytes,census.f3,census.g3,census.ft3,census.gt3,
    census.f4,census.g4,census.ft4,census.gt4,census.semi,census.raw,census.other,negative,census.first,result);
  // Groundwork is deliberately observation-only: a failed join is the diagnostic result, not a
  // reason to crash an otherwise valid generated render.  Promotion to an acceptance oracle will
  // make a nonempty mismatch fatal only after source/payload/bin joins are independently green.
}
}

void spyro_register_actor_chain_oracle(){
  const char* mode=cfg_str("PSXPORT_ACTOR_CHAIN_ORACLE");
  if(!mode||!*mode)return;
  if(std::string_view(mode)!="payload"&&std::string_view(mode)!="ot"){
    lucent::error("actorchainoracle","REFUSED: PSXPORT_ACTOR_CHAIN_ORACLE={} requires payload or ot",mode);
    abort();
  }
  if(const char* identity=cfg_str("PSXPORT_NDIFF_IDENTITY");identity&&*identity){
    lucent::error("actorchainoracle","REFUSED: PSXPORT_NDIFF_IDENTITY can overwrite the 0x8001F798 diagnostic override");abort();}
  if(const char* trace=cfg_str("PSXPORT_FNTRACE");trace&&(std::string_view(trace).find("1F798")!=std::string_view::npos||std::string_view(trace).find("1f798")!=std::string_view::npos)){
    lucent::error("actorchainoracle","REFUSED: PSXPORT_FNTRACE targets the same 0x8001F798 override slot");abort();}
  lucent::info("actorchainoracle","armed pass={} 0x8001F798; global splice remains unimplemented",mode);
  psxport_recomp()->shard_set_override(0x8001F798u,std::string_view(mode)=="ot"?actor_chain_ot_oracle:actor_chain_oracle);
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
  uint32_t reads=0;SourceSnapshot snap{};auto read=[&](uint32_t p){++reads;return p;};
  const uint32_t lastRecord=kRecordBase+(kDurableRecords-1u)*kRecordSize;
  ok=ok&&capture_source(read,lastRecord,0x801FFFD8u,0x801FFFFCu,1,2,3,4,snap)&&reads==11u&&
    snap.words.front()==0x801FFFD8u&&snap.words.back()==0x801FFFFCu&&snap.aux==0x801FFFFCu;
  auto rejects_without_read=[&](uint32_t record,uint32_t source,uint32_t aux){reads=0;SourceSnapshot reject{};
    return !capture_source(read,record,source,aux,1,2,3,4,reject)&&reads==0u;};
  ok=ok&&rejects_without_read(kRecordBase-4u,0x80001000u,0x80002000u)&&
    rejects_without_read(kRecordBase,0x801FFFDCu,0x80002000u)&&
    rejects_without_read(kRecordBase,0x80001002u,0x80002000u)&&
    rejects_without_read(kRecordBase,0x80001000u,0x801FFFFEu);
  EpochState ep{};
  const bool bWithoutA=!epoch_subset(ep,0x80001000u,kRecordBase);
  epoch_open(ep,0x80001000u,kRecordBase);const bool bOnce=epoch_subset(ep,0x80001000u,kRecordBase);
  const bool duplicateB=!epoch_subset(ep,0x80001000u,kRecordBase);
  epoch_open(ep,0x80001000u,kRecordBase);const bool changedSource=!epoch_subset(ep,0x80001004u,kRecordBase);
  epoch_open(ep,0x80001000u,kRecordBase);const bool changedRecord=!epoch_subset(ep,0x80001000u,kRecordBase+kRecordSize);
  epoch_clear(ep);const bool familyAfterFailedA=!epoch_family(ep,0x80001008u,kRecordBase,0x80001008u);
  epoch_open(ep,0x80001000u,kRecordBase);const bool wrongFamilyCursor=!epoch_family(ep,0x8000100Cu,kRecordBase,0x80001008u);
  epoch_open(ep,0x80001000u,kRecordBase);const bool wrongFamilyRecord=!epoch_family(ep,0x80001008u,kRecordBase+kRecordSize,0x80001008u);
  epoch_open(ep,0x80001000u,kRecordBase);const bool familyOnce=epoch_family(ep,0x80001008u,kRecordBase,0x80001008u);
  const bool staleFamily=!epoch_family(ep,0x80001008u,kRecordBase,0x80001008u);
  ok=ok&&bWithoutA&&bOnce&&duplicateB&&changedSource&&changedRecord&&familyAfterFailedA&&
    wrongFamilyCursor&&wrongFamilyRecord&&familyOnce&&staleFamily;
  CheckpointCensus::Expected pe{base,Family::G3,{0x06000000u,0x30112233u,0x00020001u,0x00445566u,0x00040003u,0x00778899u,0x00060005u}};
  std::vector<uint32_t> actual=pe.words;actual[0]|=0x00123456u;
  auto payloadRead=[&](uint32_t p){return actual[(p-base)/4u];};
  const auto payloadGood=compare_payloads({pe},base,base+28u,payloadRead);actual[2]^=1u;
  const auto payloadBadXy=compare_payloads({pe},base,base+28u,payloadRead);actual[2]^=1u;actual[1]^=1u;
  const auto payloadBadColor=compare_payloads({pe},base,base+28u,payloadRead);
  ok=ok&&payloadGood.compared==1&&payloadGood.mismatches==0&&payloadBadXy.mismatches==1&&
    std::string_view(payloadBadXy.first)=="xy"&&payloadBadColor.mismatches==1&&std::string_view(payloadBadColor.first)=="command_color";
  std::vector<OtEntry> otExpected{{0x80001000u,3},{0x80001020u,3},{0x80001040u,7}},otActual=otExpected;
  uint32_t otFirst=0;const char* otField="none";const bool otGood=compare_ot(otExpected,otActual,otFirst,otField);
  otActual[2].bin=8;const bool otBadBin=!compare_ot(otExpected,otActual,otFirst,otField)&&std::string_view(otField)=="bin";
  otActual=otExpected;std::swap(otActual[0].packet,otActual[1].packet);
  const bool otBadLink=!compare_ot(otExpected,otActual,otFirst,otField)&&std::string_view(otField)=="fifo";
  constexpr uint32_t ob=0x80010000u,p1=0x80020000u,p2=0x80020020u,p3=0x80020040u;
  const std::vector<OtEntry> fixtureExpected{{p1,1},{p2,2},{p3,2}};
  using Cell=std::pair<uint32_t,uint32_t>;auto runOt=[&](std::vector<Cell> mem){OtCensus x{};x.haveSource=true;x.source.localOt=ob;x.expected=fixtureExpected;
    snapshot_ot_read(x,[&](uint32_t p){for(const auto& c:mem)if(c.first==p)return c.second;return 0u;});return x;};
  const std::vector<Cell> validMem{{ob+8,p1},{ob+12,p1},{ob+16,p3},{ob+20,p2},{p2,p3&0x00FFFFFFu}};
  const auto walkGood=runOt(validMem);const bool walkPositive=walkGood.binsScanned==288&&walkGood.actual.size()==3&&
    walkGood.emptyAppend==2&&walkGood.nonemptyAppend==1&&walkGood.cycles==0&&walkGood.duplicates==0&&walkGood.outOfRange==0&&
    compare_ot(fixtureExpected,walkGood.actual,otFirst,otField);
  auto orderMem=validMem;orderMem[2].second=p2;orderMem[3].second=p3;orderMem[4]={p3,p2&0x00FFFFFFu};
  const auto walkOrder=runOt(orderMem);const bool walkBadOrder=!compare_ot(fixtureExpected,walkOrder.actual,otFirst,otField)&&std::string_view(otField)=="fifo";
  auto cycleMem=validMem;cycleMem[4].second=p2&0x00FFFFFFu;const auto walkCycle=runOt(cycleMem);const bool walkHasCycle=walkCycle.cycles>0;
  auto duplicateMem=validMem;duplicateMem.push_back({ob+24,p1});duplicateMem.push_back({ob+28,p1});
  const auto walkDuplicate=runOt(duplicateMem);const bool walkHasDuplicate=walkDuplicate.duplicates>0;
  auto rangeMem=validMem;rangeMem.push_back({ob+24,0x80200000u});rangeMem.push_back({ob+28,0x80200000u});
  const auto walkRange=runOt(rangeMem);const bool walkOutOfRange=walkRange.outOfRange>0;
  auto oneSideMem=validMem;oneSideMem.push_back({ob+24,p1});const auto walkOneSide=runOt(oneSideMem);const bool walkOneSided=walkOneSide.outOfRange>0;
  ok=ok&&otGood&&otBadBin&&otBadLink&&walkPositive&&walkBadOrder&&walkHasCycle&&walkHasDuplicate&&walkOutOfRange&&walkOneSided;
  lucent::info("selftest","{}(actorchainrecipe): packets={} bytes={} G4={} GT4={} G3={} GT3={} FT4={} semi={} raw={} corrupt_address=1 corrupt_family=1 corrupt_command={} source_valid_reads=11 source_invalid_cases=4 source_invalid_reads=0 epoch_negatives[b_without_a={} duplicate_b={} changed_source={} changed_record={} family_after_failed={} wrong_cursor={} wrong_family_record={} stale_family={}] payload_good={} corrupt_xy={} corrupt_command_color={} ot_walk[positive={} bins={} empty={} append={} corrupt_bin={} corrupt_link={} cycle={} duplicate={} out_of_range={} one_sided={}]",ok?"PASS":"FAIL",good.packets,good.bytes,good.g4,good.gt4,good.g3,good.gt3,good.ft4,good.semi,good.raw,bad.other==1u,bWithoutA,duplicateB,changedSource,changedRecord,familyAfterFailedA,wrongFamilyCursor,wrongFamilyRecord,staleFamily,payloadGood.mismatches==0,payloadBadXy.mismatches==1,payloadBadColor.mismatches==1,walkPositive,walkGood.binsScanned,walkGood.emptyAppend,walkGood.nonemptyAppend,otBadBin,walkBadOrder,walkHasCycle,walkHasDuplicate,walkOutOfRange,walkOneSided);
  return ok?0:1;
}
