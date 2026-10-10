// Included inside the reviewed command adapter context.
std::optional<Vector> fresh_shot_ray(const Plan& plan) {
    if(!firearm(plan.weapon))return {};
    using Inaccuracy=float(__fastcall*)(Address,float*,float*);using BaseSpread=float(__fastcall*)(Address);
    using Seed=std::uint32_t(__fastcall*)(Address,const Vector*,int);
    using Spread=void(__fastcall*)(std::int16_t,int,int,std::uint32_t,float,float,float,float*,float*);
    state->native_stage="shot encode accuracy";
    const float accuracy=reinterpret_cast<Inaccuracy>(state->client+inaccuracy_rva)(plan.weapon,nullptr,nullptr);
    const float spread=reinterpret_cast<BaseSpread>(state->client+base_spread_rva)(plan.weapon),recoil=read_native<float>(plan.weapon+state->f.recoil);
    if(!std::isfinite(accuracy)||!std::isfinite(spread)||!std::isfinite(recoil)||accuracy<0||spread<0)return {};
    const auto punch=aim_punch(plan.pawn);const auto seed=reinterpret_cast<Seed>(state->client+seed_rva)(0,&plan.solution.sent,plan.shot_tick);
    Vector offset{};reinterpret_cast<Spread>(state->client+spread_rva)(static_cast<std::int16_t>(read_native<std::uint16_t>(plan.weapon+state->f.definition)),1,0,seed+1,accuracy,spread,recoil,&offset.x,&offset.y);
    const auto ray=bullet(plan.solution.sent+Vector{punch.x,punch.y,0},offset);
    state->observed_accuracy=accuracy;state->observed_spread=spread;
    return finite(ray)?std::optional<Vector>(ray):std::nullopt;
}
bool apply(Address proto,const Plan& plan,Transaction& tx) {
    if(stopped || !rage_active())return false;
    const auto current=settings.load();if(!current || *current!=plan.config)return false;
    const auto base_address=read_native<Address>(proto+iris::sdk::binding_values::layout_proto_base);const auto base=read_native<Base>(base_address);
    if(base.random_seed!=plan.seed || base.tick!=plan.tick || !base.crc)return false;
    if(!movement_timeline_supported(base))return false;
    const auto history=read_native<Repeated>(proto+iris::sdk::binding_values::layout_proto_history);
    if(history.size<=plan.history_index || history.size>maximum_steps || history.capacity<history.size || history.capacity>4096 || read_native<int>(history.rep)<history.size)return false;
    const auto entry_address=read_native<Address>(history.rep+8+8*plan.history_index);
    const auto entry=read_native<History>(entry_address);
    if(entry.header.vtable!=state->client+reviewed::history_vtable || entry.player_tick!=plan.shot_tick || !(entry.has&0x800) || !entry.messages[0] || !base.angles || !base.buttons)return false;
    const auto target=sdk::entity(state->client,plan.target);
    if(!target || read_native<int>(target+state->f.health)<=0 || read_native<bool>(target+state->f.immune))return false;
    const auto controller=read_native<Address>(state->client+cosmetics::bindings::local_controller);
    if(!controller || read_native<std::uint32_t>(controller+state->f.pawn)!=plan.pawn_handle || sdk::entity(state->client,plan.pawn_handle)!=plan.pawn || read_native<int>(plan.pawn+state->f.health)<=0)return false;
    const auto services=read_native<Address>(plan.pawn+state->f.services);
    if(!services || read_native<std::uint32_t>(services+state->f.active)!=plan.weapon_handle || sdk::entity(state->client,plan.weapon_handle)!=plan.weapon)return false;
    if(read_native<int>(plan.weapon+state->f.clip)<=0||read_native<bool>(plan.weapon+state->f.reload))return false;
    const auto source=position(plan.pawn);
    const auto ray=fresh_shot_ray(plan);if(!ray)return false;
    if(!within_fov(plan.view,source,plan.point,plan.config.fov) || shot_damage(plan.pawn,plan.weapon,target,source,source+*ray*length(plan.point-source)*1.02f,plan.region,plan.config.through_walls)<combat::required_damage(plan.config.minimum_damage,read_native<int>(target+state->f.health),plan.config.health_damage)){status="Shot rejected: current seeded ray misses";return false;}
    auto angles=read_native<Angles>(base.angles),sample=read_native<Angles>(entry.messages[0]);auto buttons=read_native<Buttons>(base.buttons);const auto movement_yaw=std::remainder(plan.solution.sent.y-angles.y,360.f);
    angles.has|=7;angles.x=plan.solution.sent.x;angles.y=plan.solution.sent.y;angles.z=plan.solution.sent.z;
    sample.has|=7;sample.x=angles.x;sample.y=angles.y;sample.z=angles.z;
    if(plan.automatic){buttons.has|=3;buttons.value|=attack;buttons.changed|=attack;}
    tx.put(base.angles,angles);tx.put(entry.messages[0],sample);tx.put(base.buttons,buttons);
    tx.put(entry_address+offsetof(History,has),entry.has|1u);
    if(plan.automatic){tx.put(proto+iris::sdk::binding_values::layout_proto_presence,read_native<std::uint32_t>(proto+iris::sdk::binding_values::layout_proto_presence)|0x21u);tx.put(proto+iris::sdk::binding_values::layout_proto_attack_history,plan.history_index);}
    // Prepared steering already aligned the real local impulse; encoding rotates it once.
    const bool prepared=std::any_of(state->movement_edits.begin(),state->movement_edits.end(),[&](const auto& edit){return edit.number==plan.raw_number&&edit.tick==plan.tick&&edit.seed==plan.seed&&edit.pawn==plan.pawn_handle;});
    preserve_movement(base_address,base,movement_yaw,tx,plan.pawn,{},false,prepared);
    checksum(base_address,base,buttons,angles,tx);
    status=plan.automatic?"Auto-fire active":"Manual shot adjusted";
    return true;
}
void firing_intent(Address command,const Command& cmd,const Base& base,bool fire);
void apply_request(Address command,const Command& cmd,const Base& base,const ShotRequest& request) {
    if(request.blocked)firing_intent(command,cmd,base,false);
    if(request.scope&&base.buttons&&base.angles){
        constexpr auto secondary=1ull<<11;Transaction tx;auto buttons=read_native<Buttons>(base.buttons);
        buttons.has|=7;buttons.value|=secondary;buttons.changed|=secondary;buttons.scroll|=secondary;
        tx.put(base.buttons,buttons);tx.put(command+offsetof(Command,value),read_native<std::uint64_t>(command+offsetof(Command,value))|secondary);
        tx.put(command+offsetof(Command,changed),read_native<std::uint64_t>(command+offsetof(Command,changed))|secondary);
        tx.put(command+offsetof(Command,scroll),read_native<std::uint64_t>(command+offsetof(Command,scroll))|secondary);
        checksum(cmd.base,read_native<Base>(cmd.base),buttons,read_native<Angles>(base.angles),tx);tx.committed=true;
    }
}
void process(Address command) {
    if(!rage_active()){visuals::wall_target.store(nullptr);if(state->plan&&!state->plan->automatic)state->plan.reset();state->shot_request.reset();state->rage_gate.reset();state->processed=0;status="Rage disabled or blocked by safe mode";return;}
    if(round_frozen){visuals::wall_target.store(nullptr);state->plan.reset();state->shot_request.reset();state->rage_gate.reset();status="Paused during freeze time or unavailable round state";return;}
    const auto snapshot=settings.load();if(!snapshot)return;const auto config=*snapshot;
    const auto controller=read_native<Address>(state->client+cosmetics::bindings::local_controller);
    const auto& f=state->f;
    const auto pawn=controller?sdk::entity(state->client,read_native<std::uint32_t>(controller+f.pawn)):0;
    player_alive=pawn && read_native<int>(pawn+f.health)>0;if(!player_alive){visuals::wall_target.store(nullptr);return;}
    const auto camera=read_native<Address>(pawn+f.camera);const auto view_fov=camera?read_native<unsigned>(camera+f.camera_fov):90u;
    camera_fov=view_fov>1 && view_fov<179?static_cast<float>(view_fov):90.f;
    const auto cmd=read_native<Command>(command);if(!cmd.base)return;const auto base=read_native<Base>(cmd.base);
    state->observed_command=cmd.number;state->observed_legacy=base.command_number;state->observed_seed=base.random_seed;state->observed_history=cmd.history.size;state->observed_presence=base.has;
    if(cmd.vtable!=state->client+reviewed::command_vtable || cmd.proto.vtable!=state->client+reviewed::command_proto_vtable || cmd.number<=0 || cmd.history.size<=0 || cmd.history.size>maximum_steps || cmd.history.capacity<cmd.history.size || cmd.history.capacity>4096 || !cmd.history.rep || read_native<int>(cmd.history.rep)<cmd.history.size){status="Waiting for populated command history";return;}
    if(base.moves.size<0 || base.moves.size>maximum_steps || base.moves.capacity<base.moves.size || base.moves.capacity>4096)return;
    bool manual=(cmd.value&attack)!=0 || (base.buttons && (read_native<Buttons>(base.buttons).value&attack)!=0);
    for(int i=0;i<base.moves.size;++i){
        const auto step=read_native<Step>(read_native<Address>(base.moves.rep+8+8*i));
        if((step.button&attack) && step.pressed)manual=true;
    }
    const bool requested=shot_requested(config.auto_fire,manual);
    if(std::none_of(config.hitboxes.begin(),config.hitboxes.end(),[](bool value){return value;})){state->plan.reset();status="Select at least one hitbox";return;}
    const auto services=read_native<Address>(pawn+f.services);const auto weapon_handle=services?read_native<std::uint32_t>(services+f.active):0xffffffff;
    const auto weapon=services?sdk::entity(state->client,weapon_handle):0;const auto pawn_handle=read_native<std::uint32_t>(controller+f.pawn);
    if(state->processed==cmd.number && state->processed_tick==base.tick && state->processed_seed==base.random_seed && state->processed_pawn==pawn && state->processed_handle==pawn_handle && state->processed_weapon==weapon_handle && state->processed_config==config){
        if(state->shot_request&&state->shot_request->seed==base.random_seed)apply_request(command,cmd,base,*state->shot_request);
        return;
    }
    state->plan.reset();
    state->shot_request.reset();
    if(!weapon || read_native<int>(weapon+f.clip)<=0 || read_native<bool>(weapon+f.reload)){status="Waiting for a loaded firearm";return;}
    const int index=manual && (cmd.has&0x20) && cmd.attack_history>=0 && cmd.attack_history<cmd.history.size?cmd.attack_history:cmd.history.size-1;
    const auto entry=read_native<History>(read_native<Address>(cmd.history.rep+8+8*index));
    const auto tick=entry.player_tick;
    if(entry.header.vtable!=state->client+reviewed::history_vtable || !(entry.has&0x800) || tick<=0 || !entry.messages[0])return;
    const auto next=read_native<int>(weapon+f.next);const auto ratio=read_native<float>(weapon+f.ratio);
    const bool ready=std::isfinite(entry.player_fraction)&&entry.player_fraction>=0&&entry.player_fraction<1&&std::isfinite(ratio)&&(tick>next || (tick==next&&entry.player_fraction>=ratio));
    if(!ready || !requested){
        status=ready?"Manual mode: waiting for fire":"Waiting for weapon cooldown";
        if(!config.through_walls || !config.highlight_target){visuals::wall_target.store(nullptr);state->rage_gate.reset();return;}
        if(GetTickCount64()-state->preview_sampled<33)return;
    }
    visuals::wall_target.store(nullptr);state->preview_sampled=GetTickCount64();
    const auto vdata=read_native<Address>(weapon+iris::sdk::binding_values::layout_weapon_vdata);const auto definition=read_native<std::uint16_t>(weapon+f.definition);
    constexpr std::array firearms{1,2,3,4,7,8,9,10,11,13,14,16,17,19,23,24,26,28,30,32,33,34,36,38,39,40,60,61,63};
    if(!vdata || read_native<int>(vdata+f.bullets)!=1 || std::find(firearms.begin(),firearms.end(),definition)==firearms.end()){status="Select a firearm that fires one bullet per shot";return;}
    const auto eye=position(pawn);const auto team=read_native<std::uint8_t>(pawn+f.team);
    const auto input=read_native<Address>(state->client+cosmetics::bindings::input);
    const auto look=input?camera_angles(input):Vector{};
    if(!input || !finite(look))return;
    state->processed=cmd.number;state->processed_tick=base.tick;state->processed_seed=base.random_seed;state->processed_pawn=pawn;state->processed_handle=pawn_handle;state->processed_weapon=weapon_handle;state->processed_config=config;
    if(state->scan_tick==base.tick&&state->scan_pawn==pawn_handle&&state->scan_weapon==weapon_handle&&state->scan_config==config&&state->scan_manual==manual){++state->scan_skips;return;}
    state->scan_tick=base.tick;state->scan_pawn=pawn_handle;state->scan_weapon=weapon_handle;state->scan_config=config;state->scan_manual=manual;
    struct Candidate {Address pawn{};std::uint32_t handle{};HitboxPoint hitbox;float angle{};};
    std::vector<Candidate> candidates;candidates.reserve(256);
    for(int i=1;i<=64;++i){
        const auto other=controller_at(i);
        if(!other||other==controller||read_native<bool>(other+f.hltv))continue;
        const auto candidate_handle=read_native<std::uint32_t>(other+f.player_pawn);const auto candidate=sdk::entity(state->client,candidate_handle);
        if(!candidate||candidate==pawn||read_native<int>(candidate+f.health)<=0||read_native<bool>(candidate+f.immune)||read_native<std::uint8_t>(candidate+f.team)==team)continue;
        const auto scene=read_native<Address>(candidate+f.scene);if(!scene||read_native<bool>(scene+f.dormant))continue;
        for(const auto& hitbox:hitbox_points(scene,config.multipoint||config.through_walls,config.multipoint_scale))
            if(config.hitboxes[hitbox.region]&&within_fov(look,eye,hitbox.point,config.fov)&&candidates.size()<4096)
                candidates.push_back({candidate,candidate_handle,hitbox,angular_distance(bullet(look,{}),hitbox.point-eye)});
    }
    std::stable_sort(candidates.begin(),candidates.end(),[&](const Candidate& a,const Candidate& b){
        if(!config.multipoint&&a.hitbox.secondary!=b.hitbox.secondary)return !a.hitbox.secondary;
        return a.angle<b.angle;
    });
    float damage{};Address target{};std::uint32_t handle{};Vector point{};int region{};HitboxPoint selected;
    combat::QueryBudget scan_budget(48);
    for(const auto& candidate:candidates){
        if(!scan_budget.take()){++state->scan_exhausted;break;}
        const auto tested=shot_damage(pawn,weapon,candidate.pawn,eye,candidate.hitbox.point,candidate.hitbox.region,config.through_walls);
        if(tested<combat::required_damage(config.minimum_damage,read_native<int>(candidate.pawn+f.health),config.health_damage))continue;
        target=candidate.pawn;handle=candidate.handle;point=candidate.hitbox.point;region=candidate.hitbox.region;selected=candidate.hitbox;damage=tested;break;
    }
    ++state->scans;state->scan_candidates+=candidates.size();
    if(!target){state->rage_gate.clear_target();status="No selected target inside FOV";return;}
    if(config.through_walls && config.highlight_target && !visible(pawn,target,eye,point)){
        auto preview=std::make_shared<visuals::WallTarget>();preview->matrix=read_native<visuals::Matrix>(state->client+binding_values::client_view_matrix);
        preview->corners=selected.corners;preview->point=point;preview->sampled=GetTickCount64();preview->handle=handle;preview->damage=damage;preview->region=region;visuals::wall_target.store(std::move(preview));
    }
    if(!ready || !requested)return;
    if(state->gate_config!=config){state->rage_gate.reset();state->gate_config=config;}
    const auto now=GetTickCount64();const bool confirmed=state->rage_gate.confirm(pawn_handle,weapon_handle,handle,now,config.delay_shot?config.shot_delay:0);
    const bool sniper=definition==9||definition==11||definition==38||definition==40;
    const bool needs_scope=config.quick_scope&&sniper&&!read_native<bool>(pawn+f.scoped);
    const auto velocity=read_native<Vector>(pawn+f.velocity);const auto motion=read_native<Address>(pawn+f.movement);
    const bool can_stop=config.quick_stop&&(read_native<std::uint32_t>(pawn+f.flags)&1)&&read_native<std::uint8_t>(pawn+f.move_type)==2&&!(cmd.value&2)&&motion;
    const auto max_speed=can_stop?read_native<float>(motion+f.maximum_speed):0.f;
    const bool stopping=can_stop&&finite(velocity)&&std::isfinite(max_speed)&&max_speed>0&&std::hypot(velocity.x,velocity.y)>max_speed*.25f;
    state->shot_request=ShotRequest{cmd.number,base.tick,base.random_seed,pawn_handle,weapon_handle,config,stopping,!confirmed||needs_scope||stopping,needs_scope&&state->rage_gate.scope(false,now)};
    if(!needs_scope)state->rage_gate.scope(true,now);
    if(state->shot_request->blocked){apply_request(command,cmd,base,*state->shot_request);status=needs_scope?"Waiting for scope":stopping?"Stopping before shot":"Confirming target";return;}
    const auto desired=aim(eye,point);if(!desired)return;
    using Inaccuracy=float(__fastcall*)(Address,float*,float*);using BaseSpread=float(__fastcall*)(Address);using Punch=void(__fastcall*)(Address,Vector*,unsigned);
    const auto accuracy=reinterpret_cast<Inaccuracy>(state->client+inaccuracy_rva)(weapon,nullptr,nullptr);
    const auto spread=reinterpret_cast<BaseSpread>(state->client+base_spread_rva)(weapon);
    const auto recoil=read_native<float>(weapon+f.recoil);Vector punch{};const auto punch_services=read_native<Address>(pawn+f.punch);
    if(!punch_services || !std::isfinite(accuracy)||!std::isfinite(spread)||!std::isfinite(recoil)||accuracy<0||spread<0)return;
    reinterpret_cast<Punch>(state->client+punch_rva)(punch_services,&punch,0);
    using Seed=std::uint32_t(__fastcall*)(Address,const Vector*,int);
    using Spread=void(__fastcall*)(std::int16_t,int,int,std::uint32_t,float,float,float,float*,float*);
    const auto seed_of=[](Vector angle,int stamp){++state->seed_calls;return reinterpret_cast<Seed>(state->client+seed_rva)(0,&angle,stamp);};
    const auto spread_of=[&](std::uint32_t seed){Vector result;reinterpret_cast<Spread>(state->client+spread_rva)(static_cast<std::int16_t>(definition),1,0,seed+1,accuracy,spread,recoil,&result.x,&result.y);return result;};
    auto solution=config.compensate?solve(*desired,config.remove_recoil?punch:Vector{},tick,seed_of,spread_of,std::min(1.f,accuracy+spread)):without_spread(*desired,config.remove_recoil?punch:Vector{});
    if(!solution){status="Waiting for an exact spread solution";return;}
    const auto minimum=combat::required_damage(config.minimum_damage,read_native<int>(target+f.health),config.health_damage);
    solution->direction=bullet(solution->sent+Vector{punch.x,punch.y,0},spread_of(seed_of(solution->sent,tick)));
    if(!finite(solution->direction)||shot_damage(pawn,weapon,target,eye,eye+solution->direction*(read_native<float>(vdata+f.range)*.999f),region,config.through_walls)<minimum){status="Waiting for a valid final ray";return;}
    combat::QueryBudget chance_budget(128);const auto prior_budget=state->chance_budget;
    if(!config.compensate && !combat::chance_passes(config.hit_chance,[&](int sample){
        if(!chance_budget.take()){++state->chance_budget;return false;}
        const auto ray=bullet(solution->sent+Vector{punch.x,punch.y,0},spread_of(static_cast<std::uint32_t>(sample)));
        ++state->chance_samples;const bool hit=finite(ray) && shot_damage(pawn,weapon,target,eye,eye+ray*(read_native<float>(vdata+f.range)*.999f),region,config.through_walls)>=minimum;
        if(hit)++state->chance_hits;return hit;
    })){status=state->chance_budget!=prior_budget?"Hit chance paused: query budget exhausted":"Waiting for hit chance";return;}
    state->plan=Plan{base.random_seed,base.tick,index,tick,pawn,weapon,handle,*solution,point,look,config,!manual && config.auto_fire,region};
    state->plan->pawn_handle=read_native<std::uint32_t>(controller+f.pawn);state->plan->weapon_handle=read_native<std::uint32_t>(services+f.active);
    state->plan->raw_number=cmd.number;
    if(state->plan->automatic && base.buttons && base.angles){
        Transaction tx;auto buttons=read_native<Buttons>(base.buttons);buttons.has|=3;buttons.value|=attack;buttons.changed|=attack;
        tx.put(base.buttons,buttons);tx.put(command+offsetof(Command,value),cmd.value|attack);tx.put(command+offsetof(Command,changed),cmd.changed|attack);
        tx.put(command+offsetof(Command,has),cmd.has|0x21u);tx.put(command+offsetof(Command,attack_history),index);
        checksum(cmd.base,base,buttons,read_native<Angles>(base.angles),tx);tx.committed=true;
    }
    ++state->prepared_shots;state->observed_accuracy=accuracy;state->observed_spread=spread;state->observed_damage=damage;state->observed_target=handle;status="Shot prepared";
}
