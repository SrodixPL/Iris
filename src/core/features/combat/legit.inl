// Included inside the reviewed command adapter context.
void reset_view() {
    state->view_plan.reset();state->recoil={};state->locked_target=0;state->view_pawn=state->view_weapon=0;state->last_view={};state->shot_confirmation.reset();
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
    if(plan.target){const auto target=sdk::entity(state->client,plan.target);
        if(!target||read_native<int>(target+state->f.health)<=0||read_native<bool>(target+state->f.immune)||read_native<std::uint8_t>(target+state->f.team)==read_native<std::uint8_t>(plan.pawn+state->f.team)||!visible(plan.pawn,target,position(plan.pawn),plan.point))return false;
    }
    const auto history=read_native<Repeated>(proto+binding_values::layout_proto_history);
    if(history.size!=static_cast<int>(plan.samples.size())||history.size<0||history.size>maximum_steps||history.capacity<history.size||history.capacity>4096||(history.size&&(!history.rep||read_native<int>(history.rep)<history.size)))return false;
    Transaction local;auto& tx=transient?*transient:local;auto angles=read_native<Angles>(base.angles);
    angles.has|=7;angles.x=plan.sent.x;angles.y=plan.sent.y;angles.z=0;tx.put(base.angles,angles);
    for(int i=0;i<history.size;++i){const auto at=read_native<Address>(history.rep+8+8*i);const auto h=read_native<History>(at);
        if(h.header.vtable!=state->client+reviewed::history_vtable||!h.messages[0]||!std::isfinite(h.player_fraction)||h.player_tick!=plan.samples[i].tick||h.player_fraction!=plan.samples[i].fraction)return false;
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
    const auto snapshot=aim_settings.load();if(!snapshot)return;const auto config=*snapshot;
    const auto& f=state->f;const auto controller=read_native<Address>(state->client+cosmetics::bindings::local_controller);
    const auto pawn_handle=controller?read_native<std::uint32_t>(controller+f.pawn):0xffffffff;
    const auto pawn=controller?sdk::entity(state->client,pawn_handle):0;
    player_alive=pawn && read_native<int>(pawn+f.health)>0;
    if(!player_alive){hud_snapshot.store(nullptr);reset_view();return;}
    const auto camera=read_native<Address>(pawn+f.camera);const auto view_fov=camera?read_native<unsigned>(camera+f.camera_fov):90u;
    camera_fov=view_fov>1 && view_fov<179?static_cast<float>(view_fov):90.f;
    if(round_frozen){reset_view();return;}
    const auto cmd=read_native<Command>(command);if(!cmd.base)return;const auto base=read_native<Base>(cmd.base);
    state->observed_command=cmd.number;state->observed_legacy=base.command_number;state->observed_seed=base.random_seed;state->observed_history=cmd.history.size;state->observed_presence=base.has;
    if(cmd.vtable!=state->client+reviewed::command_vtable || cmd.proto.vtable!=state->client+reviewed::command_proto_vtable || !base.angles || !base.buttons || cmd.history.size<0 || cmd.history.size>maximum_steps || cmd.history.capacity<cmd.history.size || cmd.history.capacity>4096 || (cmd.history.size && (!cmd.history.rep || read_native<int>(cmd.history.rep)<cmd.history.size)) || base.moves.size<0 || base.moves.size>maximum_steps)return;
    const auto rage_config=settings.load();
    if(rage_active()&&rage_config&&state->shot_request&&state->shot_request->config==*rage_config&&state->shot_request->blocked&&state->shot_request->number==cmd.number&&state->shot_request->tick==base.tick&&state->shot_request->seed==base.random_seed&&state->shot_request->pawn==pawn_handle){reset_view();return;}
    if(state->view_plan && state->view_plan->raw_number==cmd.number && state->view_plan->seed==base.random_seed && state->view_plan->tick==base.tick && state->view_plan->pawn==pawn && state->view_plan->config==config && state->view_plan->rage==rage_active()) {
        if(!apply_view(command+offsetof(Command,proto),*state->view_plan))state->view_plan.reset();
        return;
    }
    if(state->plan && rage_active() && state->plan->seed==base.random_seed && state->plan->tick==base.tick){reset_view();legit_status="Rage takes priority";return;}
    const auto services=read_native<Address>(pawn+f.services);const auto weapon_handle=services?read_native<std::uint32_t>(services+f.active):0xffffffff;const auto weapon=services?sdk::entity(state->client,weapon_handle):0;
    const bool loaded=firearm(weapon) && read_native<int>(weapon+f.clip)>0 && !read_native<bool>(weapon+f.reload);
    if(!loaded){reset_view();legit_status="Waiting for a loaded firearm";return;}
    const auto input=read_native<Address>(state->client+cosmetics::bindings::input);if(!input)return;
    const auto look=camera_angles(input);if(!finite(look))return;
    const auto now=std::chrono::steady_clock::now();
    if(state->view_pawn!=pawn || state->view_weapon!=weapon || state->view_pawn_handle!=pawn_handle || state->view_weapon_handle!=weapon_handle || (state->last_view.time_since_epoch().count() && now-state->last_view>std::chrono::milliseconds(250)))reset_view();
    const float dt=state->last_view.time_since_epoch().count()?std::chrono::duration<float>(now-state->last_view).count():0;
    state->last_view=now;state->view_pawn=pawn;state->view_weapon=weapon;state->view_pawn_handle=pawn_handle;state->view_weapon_handle=weapon_handle;state->view_plan.reset();
    const auto buttons=read_native<Buttons>(base.buttons).value|cmd.value;
    bool firing=(buttons&attack)!=0 || ((cmd.has&0x20) && cmd.attack_history>=0 && cmd.attack_history<cmd.history.size),using_item=(buttons&((1ull<<5)|(1ull<<11)))!=0;
    for(int i=0;i<base.moves.size;++i) {
        if(!base.moves.rep || base.moves.capacity<base.moves.size || base.moves.capacity>4096 || read_native<int>(base.moves.rep)<base.moves.size)return;
        const auto step=read_native<Step>(read_native<Address>(base.moves.rep+8+8*i));
        if(step.pressed && (step.button&attack))firing=true;
        if(step.pressed && (step.button&((1ull<<5)|(1ull<<11))))using_item=true;
    }
    const auto& ls=config.legit;Vector sent=look,point{};std::uint32_t handle{};
    bool assisted=false;
    const auto definition=loaded?read_native<std::int16_t>(weapon+f.definition):0;
    const bool sniper=definition==9 || definition==11 || definition==38 || definition==40;
    const bool blocked=(ls.pause_flashed && read_native<float>(pawn+f.flash)>0) || (sniper && ls.scoped_only && !read_native<bool>(pawn+f.scoped));
    const bool legit_active=loaded && camera_frame && !rage_active() && !using_item && !blocked && (ls.enabled||ls.recoil_control||ls.trigger);
    if(legit_active) {
        const auto punch=aim_punch(pawn);
        const Vector recoil_punch{punch.x*ls.recoil_vertical,punch.y*ls.recoil_horizontal,0};
        sent=state->recoil.update(look,recoil_punch,firing && ls.recoil_control && ls.standalone_recoil,ls.recoil_strength);
        if(ls.enabled && (!ls.on_fire || firing)) {
            const auto eye=position(pawn);const auto team=read_native<std::uint8_t>(pawn+f.team);float nearest=181;bool locked=false;
            for(int i=1;i<=64;++i) {
                const auto other=controller_at(i);
                if(!other || other==controller || read_native<bool>(other+f.hltv))continue;
                const auto candidate_handle=read_native<std::uint32_t>(other+f.player_pawn);const auto candidate=sdk::entity(state->client,candidate_handle);
                if(!candidate || candidate==pawn || read_native<int>(candidate+f.health)<=0 || read_native<bool>(candidate+f.immune) || read_native<std::uint8_t>(candidate+f.team)==team)continue;
                const auto scene=read_native<Address>(candidate+f.scene);if(!scene || read_native<bool>(scene+f.dormant))continue;
                for(const auto& hitbox:hitbox_points(scene))if(ls.hitboxes[hitbox.region]) {
                    if(!within_fov(look,eye,hitbox.point,ls.fov))continue;
                    const bool preferred=ls.target_lock && candidate_handle==state->locked_target;
                    const auto distance=angular_distance(bullet(look,{}),hitbox.point-eye);
                    if((locked && !preferred) || (preferred==locked && distance>=nearest))continue;
                    if(!visible(pawn,candidate,eye,hitbox.point))continue;
                    locked=preferred;nearest=distance;handle=candidate_handle;point=hitbox.point;
                }
            }
            if(handle && handle!=state->locked_target){state->locked_target=handle;state->acquired=now;}
            if(handle && std::chrono::duration<float,std::milli>(now-state->acquired).count()>=ls.reaction_time) {
                if(const auto desired=aim(eye,point)) {
                    const auto goal=ls.recoil_control?*desired-recoil_punch*ls.recoil_strength:*desired;
                    sent=combat::smooth(sent,goal,dt,ls.smoothing,ls.max_speed);assisted=true;legit_status="Aim assistance active";
                }
            } else {legit_status=handle?"Waiting for target delay":"No visible target inside FOV";handle=0;}
            if(!handle && !state->locked_target)state->acquired={};
            if(!handle && nearest==181)state->locked_target=0;
        } else {state->locked_target=0;legit_status=ls.enabled?"Waiting for fire":"Recoil control only";}
    } else {state->recoil={};state->locked_target=0;legit_status=rage_active()?"Rage takes priority":"Disabled";}
    if(camera_frame && !rage_active() && loaded && !using_item) {
        const bool wants=firing || ls.trigger;
        std::uint32_t confirmed{};
        if(wants && !blocked && (ls.enabled || ls.trigger)){
            const auto eye=position(pawn);const auto punch=aim_punch(pawn);TraceResult result;
            if(trace_ray(pawn,eye,eye+bullet(sent,punch)*8192.f,result) && result.entity){
                for(int i=1;i<=64;++i){const auto other=controller_at(i);if(!other || other==controller || read_native<bool>(other+f.hltv))continue;
                    const auto candidate=read_native<std::uint32_t>(other+f.player_pawn);
                    if(sdk::entity(state->client,candidate)!=result.entity)continue;
                    if(read_native<int>(result.entity+f.health)>0 && !read_native<bool>(result.entity+f.immune) && read_native<std::uint8_t>(result.entity+f.team)!=read_native<std::uint8_t>(pawn+f.team))confirmed=candidate;
                    break;
                }
            }
        }
        const float delay=sniper?std::max(ls.shot_delay,ls.sniper_delay):ls.shot_delay;
        const bool ready=state->shot_confirmation.ready(confirmed,std::chrono::duration<double,std::milli>(now.time_since_epoch()).count(),delay);
        const bool gated=(ls.enabled && firing && (delay>0 || blocked)) || ls.trigger;
        if(gated && cmd.history.size>0){
            if(!ready && firing){firing_intent(command,cmd,base,false);firing=false;legit_status=blocked?"Assistance paused":"Waiting for shot confirmation";}
            else if(ready && !firing && ls.trigger){
                const auto history=read_native<History>(read_native<Address>(cmd.history.rep+8+8*(cmd.history.size-1)));
                const int next=read_native<int>(weapon+f.next);const float fraction=read_native<float>(weapon+f.ratio);
                if(history.player_tick>next || (history.player_tick==next && history.player_fraction>=fraction)){firing_intent(command,cmd,base,true);firing=true;}
            }
        }
    }
    const auto delta=Vector{sent.x-look.x,std::remainder(sent.y-look.y,360.f),0};
    if(!assisted && length(delta)<1e-6f)return;
    ViewPlan plan{base.random_seed,base.tick,pawn,weapon,handle,point,sent,delta,config,rage_active()};
    plan.pawn_handle=pawn_handle;plan.weapon_handle=weapon_handle;plan.raw_number=cmd.number;
    for(int i=0;i<cmd.history.size;++i) {
        const auto h=read_native<History>(read_native<Address>(cmd.history.rep+8+8*i));
        if(h.header.vtable!=state->client+reviewed::history_vtable || !h.messages[0] || !std::isfinite(h.player_fraction))return;
        const auto a=read_native<Angles>(h.messages[0]);
        if(!finite({a.x,a.y,a.z}))return;
        plan.samples.push_back({h.player_tick,h.player_fraction,combat::clamp_angles(Vector{a.x,a.y,0}+delta)});
    }
    state->view_plan=std::move(plan);
    if(!apply_view(command+offsetof(Command,proto),*state->view_plan))state->view_plan.reset();
}
