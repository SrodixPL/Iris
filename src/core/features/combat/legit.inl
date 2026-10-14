// Included inside the reviewed command adapter context.
bool smoke_obstructed(Vector eye,Vector point){
    if(!finite(eye)||!finite(point))return true;
    using Opacity=float(__fastcall*)(const Vector*,const Vector*,Vector*);
    const float opacity=reinterpret_cast<Opacity>(state->client+binding_values::combat_smoke_opacity_rva)(&eye,&point,nullptr);
    // A conservative visibility threshold, independent of smoke rendering.
    return !std::isfinite(opacity)||opacity>=.1f;
}
bool apply_trigger(Address proto,Transaction& tx){
    if(stopped||rage_active())return false;
    const auto generation=aim_settings.load();if(!generation)return false;
    const auto base_address=read_native<Address>(proto+binding_values::layout_proto_base);const auto base=read_native<Base>(base_address);
    if(!base.buttons||!base.angles)return false;
    for(auto i=state->trigger_plans.rbegin();i!=state->trigger_plans.rend();++i){
        if(i->seed!=base.random_seed||i->tick!=base.tick||i->generation!=generation)continue;
        const auto controller=read_native<Address>(state->client+cosmetics::bindings::local_controller);
        if(!controller||read_native<std::uint32_t>(controller+state->f.pawn)!=i->pawn)return false;
        const auto pawn=sdk::entity(state->client,i->pawn),weapon=sdk::entity(state->client,i->weapon),target=sdk::entity(state->client,i->target);
        if(!pawn||!weapon||!target||read_native<int>(pawn+state->f.health)<=0||read_native<int>(target+state->f.health)<=0||read_native<bool>(target+state->f.immune)||read_native<std::uint8_t>(target+state->f.team)==read_native<std::uint8_t>(pawn+state->f.team))return false;
        const auto services=read_native<Address>(pawn+state->f.services);
        if(!services||read_native<std::uint32_t>(services+state->f.active)!=i->weapon)return false;
        const auto settings=generation->resolve(read_native<std::uint16_t>(weapon+state->f.definition));if(!settings.trigger)return false;
        const auto history=read_native<Repeated>(proto+binding_values::layout_proto_history);
        if(history.size<=0||history.size>maximum_steps||history.capacity<history.size||history.capacity>4096||!history.rep||read_native<int>(history.rep)<history.size)return false;
        for(int sample=history.size-1;sample>=0;--sample){
            const auto entry=read_native<History>(read_native<Address>(history.rep+8+8*sample));
            if(entry.header.vtable!=state->client+reviewed::history_vtable||entry.player_tick!=i->player_tick||entry.player_fraction!=i->player_fraction||!entry.messages[0])continue;
            const auto view=read_native<Angles>(entry.messages[0]);
            if(std::hypot(view.x-i->view.x,std::remainder(view.y-i->view.y,360.f))>.05f)continue;
            auto buttons=read_native<Buttons>(base.buttons);buttons.value|=attack;buttons.changed|=attack;buttons.scroll&=~attack;buttons.has|=7;
            tx.put(base.buttons,buttons);tx.put(proto+binding_values::layout_proto_attack_history,sample);
            tx.put(proto+binding_values::layout_proto_presence,read_native<std::uint32_t>(proto+binding_values::layout_proto_presence)|0x20u);
            checksum(base_address,base,buttons,read_native<Angles>(base.angles),tx);return true;
        }
        return false;
    }
    return false;
}
void reset_view() {
    state->previous_camera_valid=false;state->acquisition_delay=state->trigger_delay=0;
    state->trigger_plans.clear();
    legit_weapon_definition=0;
    state->legit_decision.reset();
    state->view_plan.reset();state->view_generation.reset();state->recoil={};state->locked_target=0;state->smooth_rates={};state->view_pawn=state->view_weapon=0;state->last_view={};state->shot_confirmation.reset();state->trigger_seen=0;++state->trigger_epoch;state->aim_worker.cancel();state->trigger_worker.cancel();state->last_trigger={};
}
bool view_matches(Vector value,Vector expected) noexcept {
    return finite(value)&&finite(expected)&&std::abs(value.x-expected.x)<=1e-5f&&
        std::abs(std::remainder(value.y-expected.y,360.f))<=1e-5f&&std::abs(value.z-expected.z)<=1e-5f;
}
bool publish_view_camera(Address input,const ViewPlan& plan) {
    const auto camera=camera_angles(input);
    if(!view_matches(camera,plan.camera_source)&&!view_matches(camera,plan.sent)){
        state->previous_camera=camera;state->previous_camera_valid=finite(camera);
        return false;
    }
    publish_camera(input,plan.sent);state->previous_camera=plan.sent;state->previous_camera_valid=true;
    return true;
}
bool apply_view(Address proto,const ViewPlan& plan,Transaction* transient=nullptr) {
    if(stopped)return false;
    const auto current=aim_settings.load();if(!current||*current!=plan.config||plan.rage!=rage_active())return false;
    const auto base_address=read_native<Address>(proto+binding_values::layout_proto_base);const auto base=read_native<Base>(base_address);
    if(base.random_seed!=plan.seed||base.tick!=plan.tick||!base.angles||!base.buttons)return false;
    const auto controller=read_native<Address>(state->client+cosmetics::bindings::local_controller);
    if(!controller||read_native<std::uint32_t>(controller+state->f.pawn)!=plan.pawn_handle||sdk::entity(state->client,plan.pawn_handle)!=plan.pawn||read_native<int>(plan.pawn+state->f.health)<=0)return false;
    const auto services=read_native<Address>(plan.pawn+state->f.services);
    if(!services||read_native<std::uint32_t>(services+state->f.active)!=plan.weapon_handle||sdk::entity(state->client,plan.weapon_handle)!=plan.weapon)return false;
    if(plan.target){
        const auto definition=read_native<std::int16_t>(plan.weapon+state->f.definition);const auto ls=plan.config.resolve(definition);
        if(ls.activation==2&&(!ls.aim_key||!(GetAsyncKeyState(ls.aim_key)&0x8000)))return false;
        const auto target=sdk::entity(state->client,plan.target);
        if(!target||read_native<int>(target+state->f.health)<=0||read_native<bool>(target+state->f.immune)||read_native<std::uint8_t>(target+state->f.team)==read_native<std::uint8_t>(plan.pawn+state->f.team)||(!ls.through_walls&&!visible(plan.pawn,target,position(plan.pawn),plan.point)))return false;
        if(ls.smoke_check&&smoke_obstructed(position(plan.pawn),plan.point))return false;
    }
    const auto history=read_native<Repeated>(proto+binding_values::layout_proto_history);
    if(history.size!=static_cast<int>(plan.samples.size())||history.size<0||history.size>maximum_steps||history.capacity<history.size||history.capacity>4096||(history.size&&(!history.rep||read_native<int>(history.rep)<history.size)))return false;
    auto angles=read_native<Angles>(base.angles);
    // Rebuilds may restore the original input or retain our correction. Anything
    // else belongs to newer player input and must survive without cached writes.
    const Vector view{angles.x,angles.y,angles.z};
    if(!view_matches(view,plan.source)&&!view_matches(view,plan.command_sent))return false;
    for(int i=0;i<history.size;++i){const auto at=read_native<Address>(history.rep+8+8*i);const auto h=read_native<History>(at);
        if(h.header.vtable!=state->client+reviewed::history_vtable||!h.messages[0]||!std::isfinite(h.player_fraction)||h.player_tick!=plan.samples[i].tick||h.player_fraction!=plan.samples[i].fraction)return false;
        const auto a=read_native<Angles>(h.messages[0]);const Vector sample_view{a.x,a.y,a.z};
        if(!view_matches(sample_view,plan.samples[i].source)&&!view_matches(sample_view,plan.samples[i].sent))return false;
    }
    Transaction local;auto& tx=transient?*transient:local;
    angles.has|=7;angles.x=plan.command_sent.x;angles.y=plan.command_sent.y;angles.z=0;tx.put(base.angles,angles);
    for(int i=0;i<history.size;++i){const auto at=read_native<Address>(history.rep+8+8*i);const auto h=read_native<History>(at);
        auto a=read_native<Angles>(h.messages[0]);const auto sent=plan.samples[i].sent;a.has|=7;a.x=sent.x;a.y=sent.y;a.z=0;
        tx.put(h.messages[0],a);tx.put(at+offsetof(History,has),h.has|1u);
    }
    checksum(base_address,base,read_native<Buttons>(base.buttons),angles,tx);if(!transient)tx.committed=true;return true;
}
void firing_intent(Address command,const Command& cmd,const Base& base,bool fire) {
    if(!base.buttons || !base.angles)return;
    Transaction tx;auto buttons=read_native<Buttons>(base.buttons);buttons.has|=3;
    buttons.value=fire?buttons.value|attack:buttons.value&~attack;
    buttons.changed|=attack;tx.put(base.buttons,buttons);
    tx.put(command+offsetof(Command,value),fire?cmd.value|attack:cmd.value&~attack);
    tx.put(command+offsetof(Command,changed),cmd.changed|attack);
    tx.put(command+offsetof(Command,attack_history),fire?cmd.history.size-1:-1);
    tx.put(command+offsetof(Command,has),fire?cmd.has|0x21u:cmd.has&~0x20u);
    for(int i=0;i<base.moves.size;++i){const auto at=read_native<Address>(base.moves.rep+8+8*i);const auto step=read_native<Step>(at);
        if((step.button&attack) && step.pressed && !fire)tx.put(at+offsetof(Step,pressed),false);
    }
    checksum(cmd.base,base,buttons,read_native<Angles>(base.angles),tx);tx.committed=true;
}
void process_view(Address command,bool camera_frame=true) {
    const auto snapshot=aim_settings.load();if(!snapshot)return;const auto& config=*snapshot;
    // Profile edits must start fresh acquisition, recoil and confirmation timing.
    if(state->view_generation&&state->view_generation!=snapshot)reset_view();
    const auto& f=state->f;const auto controller=read_native<Address>(state->client+cosmetics::bindings::local_controller);
    const auto pawn_handle=controller?read_native<std::uint32_t>(controller+f.pawn):0xffffffff;
    const auto pawn=controller?sdk::entity(state->client,pawn_handle):0;
    player_alive=pawn && read_native<int>(pawn+f.health)>0;
    if(!player_alive){hud_snapshot.store(nullptr);reset_view();return;}
    const auto camera=read_native<Address>(pawn+f.camera);const auto view_fov=camera?read_native<unsigned>(camera+f.camera_fov):90u;
    camera_fov=cosmetics::camera_fov(view_fov>1 && view_fov<179?static_cast<float>(view_fov):90.f,camera);
    if(round_frozen){reset_view();return;}
    const auto cmd=read_native<Command>(command);if(!cmd.base)return;const auto base=read_native<Base>(cmd.base);
    state->observed_command=cmd.number;state->observed_legacy=base.command_number;state->observed_seed=base.random_seed;state->observed_history=cmd.history.size;state->observed_presence=base.has;
    if(cmd.vtable!=state->client+reviewed::command_vtable || cmd.proto.vtable!=state->client+reviewed::command_proto_vtable || !base.angles || !base.buttons || cmd.history.size<0 || cmd.history.size>maximum_steps || cmd.history.capacity<cmd.history.size || cmd.history.capacity>4096 || (cmd.history.size && (!cmd.history.rep || read_native<int>(cmd.history.rep)<cmd.history.size)) || base.moves.size<0 || base.moves.size>maximum_steps)return;
    if(state->view_plan && state->view_plan->raw_number==cmd.number && state->view_plan->seed==base.random_seed && state->view_plan->tick==base.tick && state->view_plan->pawn==pawn && state->view_plan->config==config && state->view_plan->rage==rage_active()) {
        if(!apply_view(command+offsetof(Command,proto),*state->view_plan)){state->view_plan.reset();state->previous_camera_valid=false;}
        return;
    }
    if(state->plan && rage_active() && state->plan->seed==base.random_seed && state->plan->tick==base.tick){reset_view();legit_status="Rage takes priority";return;}
    const auto services=read_native<Address>(pawn+f.services);const auto weapon_handle=services?read_native<std::uint32_t>(services+f.active):0xffffffff;const auto weapon=services?sdk::entity(state->client,weapon_handle):0;
    const auto vdata=weapon?read_native<Address>(weapon+binding_values::layout_weapon_vdata):0;
    const int pellets=vdata?read_native<int>(vdata+f.bullets):0;
    const bool loaded=vdata&&combat::legit_weapon(read_native<std::uint16_t>(weapon+f.definition))&&pellets>0&&pellets<=32&&read_native<int>(weapon+f.clip)>0&&!read_native<bool>(weapon+f.reload);
    if(!loaded){reset_view();legit_status="Waiting for a loaded firearm";return;}
    const auto input=read_native<Address>(state->client+cosmetics::bindings::input);if(!input)return;
    const auto look=camera_angles(input);if(!finite(look))return;
    const auto now=std::chrono::steady_clock::now();
    if(state->view_pawn!=pawn || state->view_weapon!=weapon || state->view_pawn_handle!=pawn_handle || state->view_weapon_handle!=weapon_handle || (state->last_view.time_since_epoch().count() && now-state->last_view>std::chrono::milliseconds(250)))reset_view();
    const std::array<std::int64_t,5> decision{cmd.number,base.random_seed,base.tick,pawn_handle,weapon_handle};
    if(state->legit_decision&&*state->legit_decision==decision)return;
    state->legit_decision=decision;
    const float dt=state->last_view.time_since_epoch().count()?std::chrono::duration<float>(now-state->last_view).count():0;
    state->last_view=now;state->view_pawn=pawn;state->view_weapon=weapon;state->view_pawn_handle=pawn_handle;state->view_weapon_handle=weapon_handle;state->view_plan.reset();
    state->view_generation=snapshot;
    const combat::workers::Identity worker_identity{snapshot,pawn_handle,weapon_handle,cmd.number,std::chrono::duration<double,std::milli>(now.time_since_epoch()).count()};
    const auto buttons=read_native<Buttons>(base.buttons).value|cmd.value;
    bool firing=(buttons&attack)!=0 || ((cmd.has&0x20) && cmd.attack_history>=0 && cmd.attack_history<cmd.history.size),using_item=(buttons&((1ull<<5)|(1ull<<11)))!=0;
    for(int i=0;i<base.moves.size;++i) {
        if(!base.moves.rep || base.moves.capacity<base.moves.size || base.moves.capacity>4096 || read_native<int>(base.moves.rep)<base.moves.size)return;
        const auto step=read_native<Step>(read_native<Address>(base.moves.rep+8+8*i));
        if(step.pressed && (step.button&attack))firing=true;
        if(step.pressed && (step.button&((1ull<<5)|(1ull<<11))))using_item=true;
    }
    Vector sent=look,point{};std::uint32_t handle{};
    bool assisted=false;
    const auto definition=loaded?read_native<std::int16_t>(weapon+f.definition):0;
    legit_weapon_definition=definition;
    const auto ls=config.resolve(definition);
    const bool aim_key_held=ls.activation!=2||(ls.aim_key!=0&&(GetAsyncKeyState(ls.aim_key)&0x8000)!=0);
    const bool aim_enabled=ls.enabled&&aim_key_held;
    const bool sniper=definition==9 || definition==11 || definition==38 || definition==40;
    const bool blocked=(ls.pause_flashed && read_native<float>(pawn+f.flash)>0) || (ls.air_check&&!(read_native<std::uint32_t>(pawn+f.flags)&1)) || (sniper && ls.scoped_only && !read_native<bool>(pawn+f.scoped));
    const bool legit_active=loaded && camera_frame && !rage_active() && !using_item && !blocked && (ls.enabled||ls.recoil_control||ls.trigger);
    if(legit_active) {
        const auto punch=aim_punch(pawn);
        const Vector recoil_punch{punch.x*ls.recoil_vertical,punch.y*ls.recoil_horizontal,0};
        const Vector user=state->previous_camera_valid?Vector{look.x-state->previous_camera.x,std::remainder(look.y-state->previous_camera.y,360.f),0}:Vector{};
        sent=state->recoil.update(look,recoil_punch,firing && ls.recoil_control && (ls.standalone_recoil||aim_enabled),ls.recoil_strength);
        if(aim_enabled && (ls.aim_mode==1||ls.activation!=1||firing)) {
            const auto eye=position(pawn);const auto team=read_native<std::uint8_t>(pawn+f.team);float nearest=181;bool locked=false;int preference=-1;float best_score=-1;
            struct Candidate {std::uint32_t handle{};Address entity{};Vector point{};float angle{};int rank{};bool locked{};unsigned ordinal{};float score{};int region{};};
            std::vector<Candidate> candidates;
            std::vector<Candidate> chance_candidates;
            combat::QueryBudget visibility_budget(256);
            for(int i=1;i<=64&&candidates.size()<1024;++i) {
                const auto other=controller_at(i);
                if(!other || other==controller || read_native<bool>(other+f.hltv))continue;
                const auto candidate_handle=read_native<std::uint32_t>(other+f.player_pawn);const auto candidate=sdk::entity(state->client,candidate_handle);
                if(!candidate || candidate==pawn || read_native<int>(candidate+f.health)<=0 || read_native<bool>(candidate+f.immune) || read_native<std::uint8_t>(candidate+f.team)==team)continue;
                const auto scene=read_native<Address>(candidate+f.scene);if(!scene || read_native<bool>(scene+f.dormant))continue;
                unsigned point_ordinal{};
                for(const auto& hitbox:hitbox_points(scene,std::any_of(ls.multipoint.begin(),ls.multipoint.end(),[](bool value){return value;}),.5f,true)) {
                    const auto ordinal=point_ordinal++;if(!ls.hitboxes[hitbox.region]||candidates.size()>=1024)continue;
                    if(hitbox.secondary&&!ls.multipoint[hitbox.region])continue;
                    if(ls.only_body&&(hitbox.region==0||hitbox.region>=4))continue;
                    const auto distance=angular_distance(bullet(look,{}),hitbox.point-eye);
                    if(distance>ls.effective_fov(camera_fov)*.5f)continue;
                    if(ls.target_selection==1&&ls.smoke_check){if(!visibility_budget.take())continue;if(smoke_obstructed(eye,hitbox.point))continue;}
                    const bool preferred=ls.target_lock && candidate_handle==state->locked_target;
                    // When both preferences are selected, both preferred regions
                    // compete by crosshair distance. Body-only remains a filter.
                    const int rank=(ls.prefer_head&&hitbox.region==0)||(ls.prefer_body&&hitbox.region>=1&&hitbox.region<=3)?1:0;
                    float score=0;
                    if(ls.target_selection==1){if(!visibility_budget.take())continue;score=shot_damage(pawn,weapon,candidate,eye,hitbox.point,hitbox.region,ls.through_walls);if(score<=0)continue;}
                    candidates.push_back({candidate_handle,candidate,hitbox.point,distance,rank,preferred,ordinal,score,hitbox.region});
                }
            }
            combat::workers::AimInput job;job.identity=worker_identity;job.selection=ls.target_selection;job.locked=state->locked_target;
            std::unordered_map<std::uint64_t,std::size_t> indices;
            for(std::size_t i=0;i<candidates.size();++i){const auto& c=candidates[i];job.candidates.push_back({c.handle,c.ordinal,c.angle,c.score,c.rank,c.locked});indices.emplace((std::uint64_t{c.handle}<<32)|c.ordinal,i);}
            state->aim_worker.submit(std::move(job));
            const auto ranked=state->aim_worker.result();const bool ranking_ready=ranked&&ranked->locked==state->locked_target&&combat::workers::current(ranked->identity,worker_identity);
            if(ranking_ready)for(const auto& hint:ranked->candidates){
                const auto found=indices.find((std::uint64_t{hint.handle}<<32)|hint.ordinal);if(found==indices.end())continue;
                const auto& c=candidates[found->second];
                if(ls.target_selection!=1){
                    if(ls.smoke_check){if(!visibility_budget.take())break;if(smoke_obstructed(eye,c.point))continue;}
                    if(!visibility_budget.take())break;
                    if(ls.through_walls?shot_damage(pawn,weapon,c.entity,eye,c.point,c.region,true)<=0:!visible(pawn,c.entity,eye,c.point))continue;
                }
                if(ls.target_selection==0){if(std::none_of(chance_candidates.begin(),chance_candidates.end(),[&](const auto& previous){return previous.handle==c.handle;}))chance_candidates.push_back(c);if(chance_candidates.size()==4)break;}
                else {locked=c.locked;preference=c.rank;best_score=c.score;nearest=c.angle;handle=c.handle;point=c.point;break;}
            }
            if(ls.target_selection==0&&!chance_candidates.empty()){
                // Reuse the reviewed spread function with deterministic sample
                // identities. This is a bounded estimate, not server acceptance.
                using Inaccuracy=float(__fastcall*)(Address,float*,float*);using BaseSpread=float(__fastcall*)(Address);
                using Spread=void(__fastcall*)(std::int16_t,int,int,std::uint32_t,float,float,float,float*,float*);
                state->native_stage="Legit selection accuracy";
                const float accuracy=reinterpret_cast<Inaccuracy>(state->client+inaccuracy_rva)(weapon,nullptr,nullptr),spread=reinterpret_cast<BaseSpread>(state->client+base_spread_rva)(weapon),recoil=read_native<float>(weapon+f.recoil);
                const float range=read_native<float>(vdata+f.range);
                if(std::isfinite(accuracy)&&accuracy>=0&&std::isfinite(spread)&&spread>=0&&std::isfinite(recoil)&&std::isfinite(range)&&range>0){
                    for(const auto& candidate:chance_candidates){
                        if((locked&&!candidate.locked)||(candidate.locked==locked&&candidate.rank<preference))continue;
                        const auto desired=aim(eye,candidate.point);if(!desired)continue;int hits=0;
                        state->native_stage="Legit selection spread";
                        for(std::uint32_t sample=1;sample<=32;++sample){Vector offset{};reinterpret_cast<Spread>(state->client+spread_rva)(static_cast<std::int16_t>(definition),1,0,sample,accuracy,spread,recoil,&offset.x,&offset.y);
                            const auto direction=bullet(*desired,offset);TraceResult result;if(finite(direction)&&trace_ray(pawn,eye,eye+direction*(range*.999f),result)&&result.entity==candidate.entity)++hits;
                        }
                        const float score=static_cast<float>(hits);
                        if(candidate.locked!=locked||candidate.rank>preference||score>best_score||(score==best_score&&candidate.angle<nearest)){locked=candidate.locked;preference=candidate.rank;best_score=score;nearest=candidate.angle;handle=candidate.handle;point=candidate.point;}
                    }
                }
            }
            const bool acquired=handle&&handle!=state->locked_target;
            if(acquired){state->locked_target=handle;state->acquired=now;state->smooth_rates={};state->acquisition_delay=combat::sampled_delay(ls.reaction_time,ls.reaction_max,static_cast<std::uint32_t>(cmd.number)^handle^pawn_handle);}
            const auto elapsed=std::chrono::duration<float,std::milli>(now-state->acquired).count();
            if(handle && elapsed>=state->acquisition_delay) {
                if(const auto desired=aim(eye,point)) {
                    const auto goal=ls.recoil_control?*desired-recoil_punch*ls.recoil_strength:*desired;
                    if(dt>0&&std::isfinite(dt)){
                        const float step_time=combat::workers::frame_step(dt);const Vector delta{goal.x-sent.x,std::remainder(goal.y-sent.y,360.f),0};
                        Vector step{};
                        if(ls.aim_mode==1)step=combat::assist_step(delta,user,ls.assist_strength,step_time,ls.effective_fov(camera_fov)*.5f);
                        else {
                            const float weight=ls.smoothing<=0?1.f:-std::expm1(-step_time/(.015f+.485f*ls.smoothing));
                            step=delta*weight;
                            const float amplitude=std::min(.025f,std::hypot(delta.x,delta.y)*.01f)*std::sqrt(step_time)*ls.smoothing;
                            step.x+=combat::gaussian(static_cast<std::uint32_t>(cmd.number)^handle)*amplitude;
                            step.y+=combat::gaussian(static_cast<std::uint32_t>(cmd.number)^handle^0x9e3779b9u)*amplitude;
                            step=step*(1.f/(1.f+std::hypot(user.x,user.y)/(std::max(dt,.001f)*360.f)));
                        }
                        const float magnitude=std::hypot(step.x,step.y),limit=ls.max_speed*step_time;if(magnitude>limit)step=step*(limit/magnitude);
                        sent=combat::clamp_angles(sent+step);assisted=true;legit_status="Aim assistance active";
                    }
                }
            } else {legit_status=handle?"Waiting for target delay":"No visible target inside FOV";handle=0;}
            if(!handle && !state->locked_target)state->acquired={};
            if(ranking_ready&&!handle && nearest==181)state->locked_target=0;
        } else {state->locked_target=0;legit_status=ls.enabled?(aim_key_held?"Waiting for fire":"Waiting for aim key"):"Recoil control only";}
    } else {state->recoil={};state->locked_target=0;legit_status=rage_active()?"Rage takes priority":"Disabled";}
    if(camera_frame && !rage_active() && loaded && !using_item) {
        const bool wants=firing || ls.trigger;
        std::uint32_t confirmed{},trigger_confirmed{};bool trigger_seed_hit{};
        if(wants && !blocked && aim_enabled){
            const auto eye=position(pawn);const auto punch=aim_punch(pawn);TraceResult result;
            if(trace_ray(pawn,eye,eye+bullet(sent+Vector{punch.x,punch.y,0},{})*8192.f,result) && result.entity){
                for(int i=1;i<=64;++i){const auto other=controller_at(i);if(!other || other==controller || read_native<bool>(other+f.hltv))continue;
                    const auto candidate=read_native<std::uint32_t>(other+f.player_pawn);
                    if(sdk::entity(state->client,candidate)!=result.entity)continue;
                    if(read_native<int>(result.entity+f.health)>0 && !read_native<bool>(result.entity+f.immune) && read_native<std::uint8_t>(result.entity+f.team)!=read_native<std::uint8_t>(pawn+f.team)){
                        confirmed=candidate;
                    }
                    break;
                }
            }
        }
        const float delay=ls.confirmation_delay?(sniper?std::max(ls.shot_delay,ls.sniper_delay):ls.shot_delay):0.f;
        const bool ready=state->shot_confirmation.ready(confirmed,std::chrono::duration<double,std::milli>(now.time_since_epoch()).count(),delay);
        const bool trigger_blocked=(sniper&&ls.scoped_only&&!read_native<bool>(pawn+f.scoped))||(ls.trigger_flash&&read_native<float>(pawn+f.flash)>0)||(ls.trigger_air&&!(read_native<std::uint32_t>(pawn+f.flags)&1));
        if(ls.trigger&&!trigger_blocked&&pellets==1&&cmd.history.size>0){
            const auto eye=position(pawn),punch=aim_punch(pawn);const auto data=read_native<Address>(weapon+binding_values::layout_weapon_vdata);
            const float range=data?read_native<float>(data+f.range):0.f;int region=-1;Address entity{};
            const auto history=read_native<History>(read_native<Address>(cmd.history.rep+8+8*(cmd.history.size-1)));
            if(std::isfinite(range)&&range>0&&history.header.vtable==state->client+reviewed::history_vtable&&history.messages[0]&&(history.has&0x800)&&history.player_tick>0&&std::isfinite(history.player_fraction)&&history.player_fraction>=0&&history.player_fraction<1){
                const auto sample_angles=read_native<Angles>(history.messages[0]);
                const auto firing_view=combat::clamp_angles(Vector{sample_angles.x,sample_angles.y,0}+Vector{sent.x-look.x,std::remainder(sent.y-look.y,360.f),0});
                using Inaccuracy=float(__fastcall*)(Address,float*,float*);using BaseSpread=float(__fastcall*)(Address);
                using Seed=std::uint32_t(__fastcall*)(Address,const Vector*,int);
                using Spread=void(__fastcall*)(std::int16_t,int,int,std::uint32_t,float,float,float,float*,float*);
                state->native_stage="Legit trigger accuracy";
                const float accuracy=reinterpret_cast<Inaccuracy>(state->client+inaccuracy_rva)(weapon,nullptr,nullptr),spread=reinterpret_cast<BaseSpread>(state->client+base_spread_rva)(weapon),recoil=read_native<float>(weapon+f.recoil);
                Vector direction{},seed_direction{};
                if(std::isfinite(accuracy)&&accuracy>=0&&std::isfinite(spread)&&spread>=0&&std::isfinite(recoil)){
                    state->native_stage="Legit trigger spread";const auto seed=reinterpret_cast<Seed>(state->client+seed_rva)(0,&firing_view,history.player_tick);Vector offset{};
                    reinterpret_cast<Spread>(state->client+spread_rva)(static_cast<std::int16_t>(definition),1,0,seed+1,accuracy,spread,recoil,&offset.x,&offset.y);
                    seed_direction=bullet(firing_view+Vector{punch.x,punch.y,0},offset);
                    direction=bullet(firing_view+Vector{punch.x,punch.y,0},{});
                }
                const auto damage=finite(direction)&&length(direction)>.5f?shot_damage(pawn,weapon,0,eye,eye+direction*(range*.999f),-1,ls.trigger_visibility==1,&region,&entity):0.f;
                if(damage>0&&damage>=ls.trigger_minimum_damage&&region>=0&&region<static_cast<int>(ls.trigger_hitboxes.size())&&ls.trigger_hitboxes[region]){
                    for(int i=1;i<=64;++i){const auto other=controller_at(i);if(!other)continue;const auto candidate=read_native<std::uint32_t>(other+f.player_pawn);if(sdk::entity(state->client,candidate)==entity){trigger_confirmed=candidate;break;}}
                    if(trigger_confirmed&&ls.trigger_smoke){const auto target_eye=position(entity);const auto endpoint=eye+direction*length(target_eye-eye);if(smoke_obstructed(eye,endpoint))trigger_confirmed=0;}
                    if(trigger_confirmed&&finite(seed_direction)&&length(seed_direction)>.5f){int hit=-1;Address contacted{};
                        const auto amount=shot_damage(pawn,weapon,entity,eye,eye+seed_direction*(range*.999f),-1,ls.trigger_visibility==1,&hit,&contacted);
                        trigger_seed_hit=contacted==entity&&amount>=ls.trigger_minimum_damage&&hit>=0&&hit<6&&ls.trigger_hitboxes[hit];
                    }
                    if(trigger_confirmed&&ls.trigger_hit_chance>0){
                        const bool chance=combat::chance_passes(ls.trigger_hit_chance,[&](int sample){
                            Vector offset{};reinterpret_cast<Spread>(state->client+spread_rva)(static_cast<std::int16_t>(definition),1,0,static_cast<std::uint32_t>(sample+1),accuracy,spread,recoil,&offset.x,&offset.y);
                            const auto ray=bullet(firing_view+Vector{punch.x,punch.y,0},offset);int hit=-1;Address contacted{};
                            const auto amount=shot_damage(pawn,weapon,entity,eye,eye+ray*(range*.999f),-1,ls.trigger_visibility==1,&hit,&contacted);
                            return contacted==entity&&amount>=ls.trigger_minimum_damage&&hit>=0&&hit<6&&ls.trigger_hitboxes[hit];
                        },32);
                        if(!chance)trigger_confirmed=0;
                    }
                }
            }
        }
        if(ls.trigger&&pellets!=1)legit_status="Shotgun Trigger unavailable";
        if(trigger_confirmed&&state->trigger_seen!=trigger_confirmed)state->trigger_delay=combat::sampled_delay(ls.trigger_reaction,ls.trigger_reaction_max,static_cast<std::uint32_t>(cmd.number)^trigger_confirmed^0x63d83595u);
        const auto trigger_target=ls.trigger&&!trigger_blocked?trigger_confirmed:0;
        if(state->trigger_seen!=trigger_target){state->trigger_seen=trigger_target;++state->trigger_epoch;state->trigger_worker.cancel();}
        state->trigger_worker.submit({worker_identity,state->trigger_epoch,trigger_target,state->trigger_delay+delay});
        const auto trigger_result=state->trigger_worker.result();
        const bool trigger_ready=trigger_result&&trigger_result->epoch==state->trigger_epoch&&trigger_result->target==trigger_target&&combat::workers::current(trigger_result->identity,worker_identity)&&trigger_result->ready;
        const bool interval_ready=!state->last_trigger.time_since_epoch().count()||std::chrono::duration<float,std::milli>(now-state->last_trigger).count()>=ls.trigger_interval;
        if(cmd.history.size>0){
            if(aim_enabled&&firing&&(delay>0||blocked)&&!ready){firing_intent(command,cmd,base,false);firing=false;legit_status=blocked?"Assistance paused":"Waiting for shot confirmation";}
            else if(trigger_ready&&trigger_seed_hit&&interval_ready&&!firing&&ls.trigger){
                const auto history=read_native<History>(read_native<Address>(cmd.history.rep+8+8*(cmd.history.size-1)));
                const int next=read_native<int>(weapon+f.next);const float fraction=read_native<float>(weapon+f.ratio);
                if(history.header.vtable==state->client+reviewed::history_vtable&&std::isfinite(history.player_fraction)&&history.player_fraction>=0&&history.player_fraction<1&&std::isfinite(fraction)&&fraction>=0&&fraction<1&&(history.player_tick>next||(history.player_tick==next&&history.player_fraction>=fraction))){
                    const auto sample_angles=read_native<Angles>(history.messages[0]);const auto shot_view=combat::clamp_angles(Vector{sample_angles.x,sample_angles.y,0}+Vector{sent.x-look.x,std::remainder(sent.y-look.y,360.f),0});
                    state->trigger_plans.push_back({base.random_seed,base.tick,history.player_tick,history.player_fraction,pawn_handle,weapon_handle,trigger_confirmed,shot_view,snapshot});if(state->trigger_plans.size()>150)state->trigger_plans.pop_front();
                    firing_intent(command,cmd,base,true);firing=true;state->last_trigger=now;
                }
            }
        }
    }
    state->previous_camera=sent;state->previous_camera_valid=true;
    const auto delta=Vector{sent.x-look.x,std::remainder(sent.y-look.y,360.f),0};
    if(!assisted && length(delta)<1e-6f)return;
    ViewPlan plan{base.random_seed,base.tick,pawn,weapon,handle,point,sent,delta,config,rage_active()};
    plan.pawn_handle=pawn_handle;plan.weapon_handle=weapon_handle;plan.raw_number=cmd.number;
    const auto source=read_native<Angles>(base.angles);plan.source={source.x,source.y,source.z};plan.camera_source=look;
    plan.command_sent=combat::clamp_angles(plan.source+delta);
    for(int i=0;i<cmd.history.size;++i) {
        const auto h=read_native<History>(read_native<Address>(cmd.history.rep+8+8*i));
        if(h.header.vtable!=state->client+reviewed::history_vtable || !h.messages[0] || !std::isfinite(h.player_fraction))return;
        const auto a=read_native<Angles>(h.messages[0]);
        if(!finite({a.x,a.y,a.z}))return;
        plan.samples.push_back({h.player_tick,h.player_fraction,combat::clamp_angles(Vector{a.x,a.y,0}+delta),{a.x,a.y,a.z}});
    }
    state->view_plan=std::move(plan);
    if(!apply_view(command+offsetof(Command,proto),*state->view_plan)){state->view_plan.reset();state->previous_camera_valid=false;}
}
