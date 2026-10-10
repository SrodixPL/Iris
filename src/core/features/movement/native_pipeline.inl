// Included inside the reviewed command bridge to share its restoration transaction.
void suspend(const char* error);
int report_native_fault(EXCEPTION_POINTERS* exception) noexcept;
std::array<float,2> rotate_movement(float forward,float left,float yaw) {
    return movement::rotate({forward,left},yaw);
}
bool movement_timeline_supported(const Base& base) {
    if(base.moves.size<0||base.moves.size>maximum_steps||base.moves.capacity<base.moves.size||base.moves.capacity>4096||
       (base.moves.size&&(!base.moves.rep||read_native<int>(base.moves.rep)<base.moves.size)))return false;
    float previous{};
    for(int i=0;i<base.moves.size;++i){const auto step=read_native<Step>(read_native<Address>(base.moves.rep+8+8*i));const float when=step.has&4?step.when:0;
        if(!std::isfinite(when)||when<previous||when>1)return false;previous=when;
        if((step.has&1)&&(step.button&movement_directions)&&(step.button&~movement_directions))return false;
        if(((step.has&8)&&!std::isfinite(step.forward))||((step.has&0x10)&&!std::isfinite(step.left))||
           ((step.has&0x20)&&!std::isfinite(step.pitch))||((step.has&0x40)&&!std::isfinite(step.yaw)))return false;
    }
    return true;
}
Address append_step(Address base_address,const Step& values,Transaction* tx=nullptr) {
    const auto repeated=base_address+offsetof(Base,moves);const auto moves=read_native<Repeated>(repeated);
    if(moves.size<0 || moves.size>=maximum_steps || moves.capacity<moves.size || moves.capacity>4096)throw std::runtime_error("Movement history is full or invalid");
    if(tx){Saved original{repeated+offsetof(Repeated,size),std::vector<std::byte>(sizeof(int))};std::memcpy(original.bytes.data(),&moves.size,sizeof(int));tx->saved.push_back(std::move(original));}
    Address step{};
    if(moves.rep && read_native<int>(moves.rep)>moves.size){step=read_native<Address>(moves.rep+8+8*moves.size);if(step && !write_native(repeated+offsetof(Repeated,size),moves.size+1))throw std::runtime_error("Cannot append reused movement step");}
    if(!step){
        using Allocate=Address(__fastcall*)(Address);using Append=Address(__fastcall*)(Address,Address);
        step=reinterpret_cast<Allocate>(state->client+binding_values::movement_step_allocate)(moves.arena);
        if(!step || reinterpret_cast<Append>(state->client+binding_values::movement_step_append)(repeated,step)!=step)throw std::runtime_error("Native movement allocation failed");
    }
    auto value=values;value.header=read_native<Header>(step);
    if(tx){tx->put(step,value);return step;}
    if(!write_native(step,value))throw std::runtime_error("Cannot initialize native movement step");
    return step;
}
std::uint64_t jump_signature(const Base& base) {
    std::uint64_t hash=1469598103934665603ull;
    const auto bytes=[&](const auto& value){for(const auto byte:std::as_bytes(std::span{&value,std::size_t{1}})){hash^=std::to_integer<unsigned char>(byte);hash*=1099511628211ull;}};
    if(base.moves.size<0||base.moves.size>maximum_steps||base.moves.capacity<base.moves.size||base.moves.capacity>4096||
       (base.moves.size&&(!base.moves.rep||read_native<int>(base.moves.rep)<base.moves.size)))throw std::runtime_error("Invalid jump timeline");
    if(base.buttons){const auto buttons=read_native<Buttons>(base.buttons);const auto mask=buttons.value&6ull;bytes(mask);}
    for(int i=0;i<base.moves.size;++i){const auto step=read_native<Step>(read_native<Address>(base.moves.rep+8+8*i));
        if((step.has&1)&&(step.button&6ull)){const auto button=step.button&6ull;bytes(button);bytes(step.pressed);bytes(step.when);}}
    return hash;
}
struct LandingProbe {float fraction{1};Vector normal{};bool solid{};};
std::optional<LandingProbe> movement_probe(Address pawn,Vector start,Vector end) {
    const auto& f=state->f;const auto services=read_native<Address>(pawn+f.movement);if(!services)return {};
    struct Hull {Vector minimum,maximum;};
    const Hull hull{read_native<Vector>(pawn+f.collision+f.minimum),read_native<Vector>(pawn+f.collision+f.maximum)};
    if(!finite(start)||!finite(end)||!finite(hull.minimum)||!finite(hull.maximum))return {};
    alignas(16) std::array<std::byte,0x48> filter{};TraceResult trace;
    using Filter=void(__fastcall*)(void*,Address,std::uint64_t,int);
    using Trace=void(__fastcall*)(Address,TraceResult*,const Vector*,const Vector*,const Hull*,void*);
    auto mask=read_native<std::uint64_t>(pawn+f.collision+f.interacts);if(read_native<std::uint32_t>(pawn+f.flags)&0x10)mask|=0x20;
    reinterpret_cast<Filter>(state->client+binding_values::movement_filter)(filter.data(),pawn,mask,8);
    state->native_stage="movement planner hull probe";
    reinterpret_cast<Trace>(state->client+binding_values::movement_hull)(services+binding_values::movement_trace_context,&trace,&start,&end,&hull,filter.data());
    LandingProbe result;std::memcpy(&result.normal,reinterpret_cast<const std::byte*>(&trace)+0x90,sizeof(result.normal));
    std::memcpy(&result.fraction,reinterpret_cast<const std::byte*>(&trace)+0xac,4);result.solid=reinterpret_cast<const std::byte*>(&trace)[0xb6]!=std::byte{};
    if(!finite(result.normal)||!std::isfinite(result.fraction)||result.fraction<0||result.fraction>1)return {};
    return result;
}
bool write_jump_plan(Address command,const Command& cmd,const Base& base,JumpPlan& plan) {
    const auto signature=jump_signature(base);if(plan.written&&signature==plan.written)return true;
    if(signature!=plan.original){if(!plan.resilient_jump)return false;plan.original=signature;}
    if(!plan.owns_jump&&!plan.owns_duck)return true;
    constexpr std::uint64_t jump=2,duck=4;std::vector<Step> steps;float previous{};
    for(int i=0;i<base.moves.size;++i){auto step=read_native<Step>(read_native<Address>(base.moves.rep+8+8*i));
        const float when=step.has&4?step.when:0;
        if(!std::isfinite(when)||when<previous||when>1)return false;previous=when;
        const auto owned=(plan.owns_jump?jump:0)|(plan.owns_duck?duck:0);
        if((step.has&1)&&(step.button&owned)){
            step.button&=~owned;
            if(!step.button){step.has&=~3u;step.pressed=false;
                // Button-bearing native steps do not consume analog movement.
                step.forward=step.left=0;step.has&=~0x18u;
                if(!(step.has&0x60))continue;
            }
        }
        steps.push_back(step);
    }
    for(const auto& action:plan.actions){Step step;step.has=7;step.button=action.button;step.pressed=action.pressed;step.when=action.when;steps.push_back(step);}
    if(steps.size()>maximum_steps)return false;
    std::stable_sort(steps.begin(),steps.end(),[](const Step& a,const Step& b){return a.when<b.when;});
    Transaction tx;
    while(read_native<Base>(cmd.base).moves.size<static_cast<int>(steps.size()))append_step(cmd.base,Step{},&tx);
    const auto moves=read_native<Base>(cmd.base).moves;
    for(std::size_t i=0;i<steps.size();++i){const auto at=read_native<Address>(moves.rep+8+8*i);auto step=steps[i];step.header=read_native<Step>(at).header;step.cached=0;tx.put(at,step);}
    tx.put(cmd.base+offsetof(Base,moves)+offsetof(Repeated,size),static_cast<int>(steps.size()));
    auto buttons=read_native<Buttons>(base.buttons);if(plan.owns_jump){buttons.value=(buttons.value&~jump)|(plan.jump?jump:0);buttons.changed&=~jump;buttons.scroll&=~jump;}
    if(plan.owns_duck){buttons.value&=~duck;buttons.changed&=~duck;buttons.scroll&=~duck;}
    buttons.has|=7;tx.put(base.buttons,buttons);
    const auto raw_owned=(plan.owns_jump?jump:0)|(plan.owns_duck?duck:0);
    tx.put(command+offsetof(Command,value),(cmd.value&~raw_owned)|(plan.jump?jump:0));
    tx.put(command+offsetof(Command,changed),cmd.changed&~raw_owned);tx.put(command+offsetof(Command,scroll),cmd.scroll&~raw_owned);
    checksum(cmd.base,read_native<Base>(cmd.base),buttons,read_native<Angles>(base.angles),tx);
    plan.written=jump_signature(read_native<Base>(cmd.base));tx.committed=true;return true;
}
void plan_jumps(Address command,const movement::Settings& config,bool held) {
    const auto cmd=read_native<Command>(command);
    if(!cmd.base||cmd.vtable!=state->client+reviewed::command_vtable||cmd.proto.vtable!=state->client+reviewed::command_proto_vtable)return;
    const auto base=read_native<Base>(cmd.base);
    if(!base.buttons||!base.angles||!movement_timeline_supported(base)){movement_status="Movement paused: unsupported input timeline";return;}
    const auto controller=read_native<Address>(state->client+cosmetics::bindings::local_controller);if(!controller)return;
    const auto handle=read_native<std::uint32_t>(controller+state->f.pawn);const auto pawn=sdk::entity(state->client,handle);
    if(!pawn||read_native<int>(pawn+state->f.health)<=0||read_native<std::uint8_t>(pawn+state->f.move_type)!=2){state->jump_state={};state->jump_plan.reset();state->jump_plans.clear();return;}
    for(auto i=state->jump_plans.rbegin();i!=state->jump_plans.rend();++i)
        if(i->number==cmd.number&&i->tick==base.tick&&i->seed==base.random_seed&&i->pawn==handle&&i->config==config&&i->held==held){
            state->jump_plan=*i;
            if(!write_jump_plan(command,cmd,base,*state->jump_plan))movement_status="Movement paused: jump input changed during rebuild";
            return;
        }
    MovementObservation sample;sample.number=cmd.number;sample.tick=base.tick;sample.seed=base.random_seed;sample.pawn=handle;
    sample.grounded=(read_native<std::uint32_t>(pawn+state->f.flags)&1)!=0;sample.velocity=read_native<Vector>(pawn+state->f.velocity);
    const auto scene=read_native<Address>(pawn+state->f.scene),services=read_native<Address>(pawn+state->f.movement);
    if(!scene||!services||!finite(sample.velocity))return;
    sample.origin=read_native<Vector>(scene+state->f.origin);sample.duck=read_native<float>(services+state->f.duck);
    for(auto i=state->movement_observations.rbegin();i!=state->movement_observations.rend();++i)
        if(i->number==cmd.number&&i->tick==base.tick&&i->seed==base.random_seed&&i->pawn==handle){sample=*i;break;}
    if(!finite(sample.origin))return;
    movement::LandingInput input;input.pawn=handle;input.held=held;input.bhop=config.bhop;input.options=config.options;
    input.grounded=sample.grounded;
    input.horizontal_speed=std::hypot(sample.velocity.x,sample.velocity.y);input.vertical_speed=sample.velocity.z;
    state->observed_support=-1;constexpr float dt=1.f/64.f;
    if(!sample.grounded&&sample.velocity.z<=0&&(config.bhop&&held)){
        const auto gravity_data=read_native<Address>(state->client+binding_values::movement_gravity+8);
        if(gravity_data){const float gravity=read_native<float>(gravity_data+0x58)*read_native<float>(pawn+state->f.gravity_scale);
            if(std::isfinite(gravity)&&gravity>=0&&gravity<=4000){
                const auto landing=movement_probe(pawn,sample.origin,sample.origin+sample.velocity*dt-Vector{0,0,gravity*dt*dt*.5f+2});
                const float normal=.7f;
                if(landing&&!landing->solid&&landing->normal.z>=normal&&landing->fraction>0&&landing->fraction<1)input.contact=std::clamp(landing->fraction,.001f,.99f);
            }
        }
    }
    const auto decision=movement::plan_landing(state->jump_state,input);
    JumpPlan plan;plan.number=cmd.number;plan.tick=base.tick;plan.seed=base.random_seed;plan.pawn=handle;plan.config=config;
    plan.held=held;plan.original=jump_signature(base);plan.owns_jump=decision.owns_jump;plan.owns_duck=decision.owns_duck;plan.jump=decision.jump;
    plan.resilient_jump=decision.owner==movement::LandingOwner::bhop;
    for(unsigned i=0;i<decision.count;++i)plan.actions.push_back({decision.actions[i].button,decision.actions[i].pressed,decision.actions[i].when});
    state->observed_held=held;state->observed_grounded=sample.grounded;state->observed_velocity=sample.velocity;
    if(decision.retry)++state->jump_retries;
    state->jump_plan=std::move(plan);
    if(!write_jump_plan(command,cmd,base,*state->jump_plan))movement_status="Movement paused: native timeline capacity or input changed";
    else if(decision.owns_jump||decision.owns_duck)++state->jump_writes;
    state->jump_plans.push_back(*state->jump_plan);if(state->jump_plans.size()>150)state->jump_plans.pop_front();
}

void preserve_movement(Address address,Base base,float yaw,Transaction& tx,Address pawn,std::optional<std::array<float,2>> assisted,bool steered_timeline=false,bool prepared_alignment=false);
std::optional<movement::AirPhysics> air_physics(Address services) {
    const auto read=[]<class T>(Address at,T& value){return memory::read_local(at,std::as_writable_bytes(std::span{&value,std::size_t{1}}));};
    Address accelerate{},cap{};movement::AirPhysics result;
    if(!services||!read(state->client+binding_values::movement_air_accelerate+8,accelerate)||!accelerate||
       !read(state->client+binding_values::movement_air_cap+8,cap)||!cap||
       !read(accelerate+0x58,result.accelerate)||!read(cap+0x58,result.cap)||
       !read(services+state->f.maximum_speed,result.maximum)||!read(services+state->f.friction,result.friction))return {};
    if(!std::isfinite(result.accelerate)||result.accelerate<0||result.accelerate>10000||!std::isfinite(result.cap)||result.cap<0||result.cap>10000||
       !std::isfinite(result.maximum)||result.maximum<=0||result.maximum>10000||!std::isfinite(result.friction)||result.friction<0||result.friction>10)return {};
    return result;
}
void capture_direction_input(Address command) {
    if(state->prediction_active)return;
    const auto config=movement_settings.load();if(!config||!(config->keys||config->rage_stop))return;
    const auto cmd=read_native<Command>(command);
    if(cmd.vtable!=state->client+reviewed::command_vtable||!cmd.base)return;
    const auto base=read_native<Base>(cmd.base);
    if(!base.buttons||!movement_timeline_supported(base))return;
    const auto buttons=read_native<Buttons>(base.buttons);
    const auto directions=cmd.value&movement_directions;
    if(config!=state->input_generation){state->input_generation=config;state->physical_input_valid=false;}
    const auto changed=(cmd.changed|buttons.changed)&movement_directions;
    if(!state->physical_input_valid||directions!=state->last_steering_directions)state->physical_directions=directions;
    else state->physical_directions=(state->physical_directions&~changed)|(directions&changed);
    for(int i=0;i<base.moves.size;++i){const auto step=read_native<Step>(read_native<Address>(base.moves.rep+8+8*i));
        if((step.has&3)==3&&(step.button&movement_directions))state->physical_directions=step.pressed?state->physical_directions|step.button:state->physical_directions&~step.button;
    }
    state->physical_directions&=movement_directions;state->physical_input_valid=true;
    const auto now=GetTickCount();
    native_input.sample(0,float(bool(state->physical_directions&8))-float(bool(state->physical_directions&16)),now);
    native_input.sample(1,float(bool(state->physical_directions&512))-float(bool(state->physical_directions&1024)),now);
}
void movement_command(Address command) {
    const auto config=movement_settings.load();
    if(config!=state->movement_generation){
        state->movement_generation=config;state->steering={};state->movement_axes.reset();state->movement_number=0;
        state->jump_state={};state->jump_plan.reset();state->jump_plans.clear();state->steering_plans.clear();state->steering_steps.clear();
    }
    if(!config||!(config->keys||config->rage_stop))return;
    if(round_frozen){
        state->steering={};state->movement_axes.reset();state->jump_state={};state->jump_plan.reset();state->jump_plans.clear();state->steering_plans.clear();state->movement_observations.clear();
        movement_status="Paused during freeze time or unavailable round state";return;
    }
    const auto cmd=read_native<Command>(command);if(!cmd.base||cmd.vtable!=state->client+reviewed::command_vtable||cmd.proto.vtable!=state->client+reviewed::command_proto_vtable)return;
    const auto base=read_native<Base>(cmd.base);if(!base.buttons||!base.angles||!movement_timeline_supported(base))return;
    const auto& f=state->f;const auto controller=read_native<Address>(state->client+cosmetics::bindings::local_controller);if(!controller)return;
    const auto handle=read_native<std::uint32_t>(controller+f.pawn);const auto pawn=sdk::entity(state->client,handle);
    if(!pawn||read_native<int>(pawn+f.health)<=0||read_native<std::uint8_t>(pawn+f.move_type)!=2){state->steering={};state->steering_plans.clear();return;}
    if(state->movement_handle!=handle){state->steering={};state->steering_plans.clear();state->movement_handle=handle;}
    const auto input=read_native<Address>(state->client+cosmetics::bindings::input);const auto services=read_native<Address>(pawn+f.movement);if(!input||!services)return;
    const auto look=camera_angles(input);auto velocity=read_native<Vector>(pawn+f.velocity);if(!finite(look)||!finite(velocity))return;
    bool grounded=(read_native<std::uint32_t>(pawn+f.flags)&1)!=0;
    for(auto i=state->movement_observations.rbegin();i!=state->movement_observations.rend();++i)
        if(i->number==cmd.number&&i->tick==base.tick&&i->seed==base.random_seed&&i->pawn==handle){grounded=i->grounded;velocity=i->velocity;break;}
    auto keys=read_native<Buttons>(base.buttons).value|cmd.value;const auto native=native_input.read(GetTickCount());
    const auto physical=native.value_or(movement::directions(keys));state->observed_native_input=bool(native);state->observed_input=physical;
    if(native){keys&=~movement_directions;keys|=(physical[0]>.001f?8:physical[0]<-.001f?16:0)|(physical[1]>.001f?512:physical[1]<-.001f?1024:0);}
    const auto physics=air_physics(services);state->observed_cap=physics?physics->cap:0;state->observed_acceleration=physics?physics->accelerate:0;
    const auto rage_config=settings.load();const auto& shot=state->shot_request;const auto weapons=read_native<Address>(pawn+f.services);
    const bool rage_stop=rage_active()&&rage_config&&shot&&shot->config==*rage_config&&shot->stop&&shot->number==cmd.number&&shot->tick==base.tick&&shot->seed==base.random_seed&&shot->pawn==handle&&weapons&&read_native<std::uint32_t>(weapons+f.active)==shot->weapon;
    std::optional<SteeringPlan> plan;
    for(auto i=state->steering_plans.rbegin();i!=state->steering_plans.rend();++i)
        if(i->number==cmd.number&&i->tick==base.tick&&i->seed==base.random_seed&&i->pawn==handle&&i->config==*config&&i->rage_stop==rage_stop){plan=*i;break;}
    if(!plan){
        std::vector<movement::SteeringEvent> events;events.reserve(base.moves.size);
        for(int i=0;i<base.moves.size;++i){const auto step=read_native<Step>(read_native<Address>(base.moves.rep+8+8*i));
            events.push_back({step.has&4?step.when:0,step.has&0x40?step.yaw:0,step.has&8?step.forward:0,step.has&0x10?step.left:0,step.has&1?step.button:0,step.has&2?step.pressed:false});
        }
        const bool jumping=(GetAsyncKeyState(VK_SPACE)&0x8000)!=0||(keys&2)!=0;
        const auto decision=movement::plan_steering(*config,state->steering,look.y,{velocity.x,velocity.y},physical,keys,grounded,jumping,(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0,rage_stop,events,physics);
        if(!decision){state->movement_axes.reset();return;}
        plan=SteeringPlan{cmd.number,base.tick,base.random_seed,handle,*config,rage_stop,look.y,*decision};
        state->steering_plans.push_back(*plan);if(state->steering_plans.size()>150)state->steering_plans.pop_front();
    }
    const float basis=read_native<Angles>(base.angles).y,delta=std::remainder(basis-plan->yaw,360.f);
    const auto initial=movement::rotate(plan->decision.initial,delta);
    std::vector<Step> output;output.reserve(base.moves.size+plan->decision.events.size());
    for(int i=0;i<base.moves.size;++i){auto step=read_native<Step>(read_native<Address>(base.moves.rep+8+8*i));
        if(!(step.has&1)||!step.button||(step.button&movement_directions)){step.button=0;step.pressed=false;step.has=(step.has&~3u)|0x1cu;step.forward=step.left=0;}
        output.push_back(step);
    }
    for(const auto& event:plan->decision.events){const auto axes=movement::rotate(event.delta,delta);
        auto at=std::find_if(output.begin(),output.end(),[&](const auto& step){return (!(step.has&1)||!step.button)&&(step.has&4?step.when:0)==event.when;});
        if(at!=output.end()){at->has|=0x1c;at->forward+=axes[0];at->left+=axes[1];}
        else{Step step;step.has=0x1c;step.when=event.when;step.forward=axes[0];step.left=axes[1];output.push_back(step);}
    }
    if(output.size()>=maximum_steps){movement_status="Movement paused: no native timeline capacity";return;}
    std::stable_sort(output.begin(),output.end(),[](const auto& a,const auto& b){return (a.has&4?a.when:0)<(b.has&4?b.when:0);});
    Transaction tx;while(read_native<Base>(cmd.base).moves.size<static_cast<int>(output.size()))append_step(cmd.base,Step{},&tx);
    const auto moves=read_native<Base>(cmd.base).moves;
    for(std::size_t i=0;i<output.size();++i){const auto at=read_native<Address>(moves.rep+8+8*i);output[i].header=read_native<Step>(at).header;output[i].cached=0;tx.put(at,output[i]);}
    tx.put(cmd.base+offsetof(Base,moves)+offsetof(Repeated,size),static_cast<int>(output.size()));
    preserve_movement(cmd.base,read_native<Base>(cmd.base),0,tx,pawn,initial,!plan->decision.events.empty());
    const auto buttons=read_native<Buttons>(base.buttons);state->last_steering_directions=buttons.value&movement_directions;
    tx.put(command+offsetof(Command,value),buttons.value);tx.put(command+offsetof(Command,changed),buttons.changed);tx.put(command+offsetof(Command,scroll),buttons.scroll);
    const auto final=read_native<Base>(cmd.base);MovementEdit edit{cmd.number,base.tick,base.random_seed,handle,{final.forward,final.left},state->observed_impulse,*config};
    std::erase_if(state->movement_edits,[&](const auto& old){return old.number==edit.number&&old.tick==edit.tick&&old.seed==edit.seed;});
    state->movement_edits.push_back(edit);if(state->movement_edits.size()>150)state->movement_edits.pop_front();
    state->movement_axes=initial;state->movement_number=cmd.number;state->movement_tick=base.tick;state->movement_seed=base.random_seed;state->movement_config=*config;state->movement_basis_yaw=basis;
    checksum(cmd.base,final,buttons,read_native<Angles>(base.angles),tx);tx.committed=true;++state->movement_commands;movement_status="Movement planner active";
}
std::array<float,2> key_axes(std::uint64_t keys) {
    return {float(bool(keys&(1ull<<3)))-float(bool(keys&(1ull<<4))),float(bool(keys&(1ull<<9)))-float(bool(keys&(1ull<<10)))};
}

std::uint64_t movement_buttons(const std::array<float,2>& axes) {
    return (axes[0]>1e-5f?1ull<<3:axes[0]<-1e-5f?1ull<<4:0)|(axes[1]>1e-5f?1ull<<9:axes[1]<-1e-5f?1ull<<10:0);
}
void movement_button_state(Address address,std::uint64_t directions,Transaction& tx) {
    auto buttons=read_native<Buttons>(address);buttons.value=(buttons.value&~movement_directions)|directions;
    buttons.changed&=~movement_directions;buttons.scroll&=~movement_directions;buttons.has|=7;tx.put(address,buttons);
}
void preserve_movement(Address address,const Base base,float yaw,Transaction& tx,Address pawn=0,std::optional<std::array<float,2>> assisted={},bool steered_timeline,[[maybe_unused]] bool prepared_alignment) {
    if(base.moves.size<0 || base.moves.size>maximum_steps || base.moves.capacity<base.moves.size || base.moves.capacity>4096 || (base.moves.size && (!base.moves.rep || read_native<int>(base.moves.rep)<base.moves.size)))throw std::runtime_error("Invalid movement history");
    if(!std::isfinite(base.forward)||!std::isfinite(base.left))throw std::runtime_error("Invalid movement values");
    constexpr auto directions=movement_directions;
    std::array<Step,maximum_steps> steps{};bool analog{},key_events{};
    for(int i=0;i<base.moves.size;++i){steps[i]=read_native<Step>(read_native<Address>(base.moves.rep+8+8*i));auto& step=steps[i];
        if(!(step.has&1))step.button=0;
        if(!(step.has&2))step.pressed=false;
        if(!(step.has&4))step.when=0;
        if(!(step.has&8))step.forward=0;
        if(!(step.has&0x10))step.left=0;
        if(!std::isfinite(step.forward)||!std::isfinite(step.left)||!std::isfinite(step.when)||step.when<0||step.when>1)throw std::runtime_error("Invalid subtick movement");
        key_events|=(step.button&directions)!=0;analog|=(step.has&0x18)!=0 || step.forward!=0 || step.left!=0 || (step.button&directions)!=0;
    }
    const auto services=pawn && state->f.movement?read_native<Address>(pawn+state->f.movement):0;
    if(!services){std::array<movement::Event,maximum_steps> events{};
        for(int i=0;i<base.moves.size;++i)events[i]={steps[i].when,steps[i].has&0x40?steps[i].yaw:0,steps[i].button?0:steps[i].forward,steps[i].button?0:steps[i].left};
        const auto corrected=movement::correct({base.forward,base.left},yaw,std::span(events.data(),base.moves.size));
        if(!corrected)throw std::runtime_error("Unsupported movement timeline");
        tx.put(address+offsetof(Base,forward),corrected->base[0]);tx.put(address+offsetof(Base,left),corrected->base[1]);tx.put(address+offsetof(Base,has),base.has|0xc0u);
        if(pawn && base.buttons)movement_button_state(base.buttons,movement_buttons(corrected->base),tx);
        for(int i=0;i<base.moves.size;++i){if(steps[i].button)continue;const auto at=read_native<Address>(base.moves.rep+8+8*i);const auto delta=corrected->deltas[i];
            if(delta[0]!=steps[i].forward || delta[1]!=steps[i].left){tx.put(at+offsetof(Step,forward),delta[0]);tx.put(at+offsetof(Step,left),delta[1]);tx.put(at+offsetof(Step,has),steps[i].has|0x18u);}
        }
        return;
    }
    std::array<float,2> actual{read_native<float>(services+state->f.command_forward),read_native<float>(services+state->f.command_left)};
    if(!std::isfinite(actual[0])||!std::isfinite(actual[1]))throw std::runtime_error("Invalid previous movement impulse");
    if(state->f.movement_button_value){const auto held=read_native<std::uint64_t>(services+state->f.movement_button_value);
        if(!(held&0x18))actual[0]=0;if(!(held&0x600))actual[1]=0;
    }
    actual=movement::limit(actual);
    auto keys=base.buttons?read_native<Buttons>(base.buttons).value:0;const auto terminal_keys=key_axes(keys);
    std::array<float,2> wish{base.has&0x40?base.forward:terminal_keys[0],base.has&0x80?base.left:terminal_keys[1]};
    if(assisted)wish=*assisted;
    auto source=wish;std::array<float,2> gain{1,1};
    if(key_events&&!assisted){
        for(int axis=0;axis<2;++axis)if(terminal_keys[axis]!=0&&wish[axis]!=0)gain[axis]=wish[axis]/terminal_keys[axis];
        for(int i=base.moves.size-1;i>=0;--i){const auto bits=steps[i].button&directions;if(bits)keys=steps[i].pressed?keys&~bits:keys|bits;}
        source=key_axes(keys);for(int axis=0;axis<2;++axis)source[axis]*=gain[axis];
    }else if(!assisted&&analog){
        // Recover the initial request from the terminal analog value and its timed deltas.
        for(int i=0;i<base.moves.size;++i)if(!steps[i].button&&steps[i].when>0){source[0]-=steps[i].forward;source[1]-=steps[i].left;}
    }
    const auto rotated=[&]{return movement::limit(rotate_movement(source[0],source[1],yaw));};
    auto previous=rotated();const auto initial=previous;
    std::vector<Step> output;output.reserve(base.moves.size+1);
    for(int i=0;i<base.moves.size;++i){auto step=steps[i];
        if(!assisted){
            if(step.button&directions){keys=step.pressed?keys|step.button:keys&~step.button;source=key_axes(keys);for(int axis=0;axis<2;++axis)source[axis]*=gain[axis];}
            else if(!step.button&&step.when>0){source[0]+=step.forward;source[1]+=step.left;}
        }else if(steered_timeline&&!step.button){source[0]+=step.forward;source[1]+=step.left;}
        const auto next=rotated();
        if(!step.button||(step.button&directions)){
            step.button=0;step.pressed=false;step.has=(step.has&~3u)|0x1cu;step.forward=next[0]-previous[0];step.left=next[1]-previous[1];
        }
        output.push_back(step);previous=next;
    }
    // RunCommand's first SetupMove uses the preceding impulse; UpdateInputState follows that movement pass.
    const movement::Axes alignment{initial[0]-actual[0],initial[1]-actual[1]};
    if(std::abs(alignment[0])>1e-6f||std::abs(alignment[1])>1e-6f){
        if(!output.empty()&&!output.front().button&&output.front().when==0){output.front().forward+=alignment[0];output.front().left+=alignment[1];output.front().has|=0x1c;}
        else{Step first;first.has=0x1c;first.forward=alignment[0];first.left=alignment[1];output.insert(output.begin(),first);}
    }
    if(output.size()>maximum_steps)throw std::runtime_error("Movement alignment exceeds native timeline capacity");
    while(read_native<Base>(address).moves.size<static_cast<int>(output.size()))append_step(address,Step{},&tx);
    const auto moves=read_native<Base>(address).moves;
    for(std::size_t i=0;i<output.size();++i){const auto at=read_native<Address>(moves.rep+8+8*i);output[i].header=read_native<Step>(at).header;output[i].cached=0;tx.put(at,output[i]);}
    tx.put(address+offsetof(Base,moves)+offsetof(Repeated,size),static_cast<int>(output.size()));
    const auto terminal=assisted?previous:movement::limit(rotate_movement(wish[0],wish[1],yaw));
    tx.put(address+offsetof(Base,forward),terminal[0]);tx.put(address+offsetof(Base,left),terminal[1]);tx.put(address+offsetof(Base,has),base.has|0xc0u);
    movement_button_state(base.buttons,movement_buttons(terminal),tx);
    state->observed_keys=terminal_keys[0]!=0||terminal_keys[1]!=0?movement_buttons(terminal_keys):0;
    state->observed_wish=wish;state->observed_impulse=actual;state->observed_move=previous;state->observed_steps=static_cast<int>(output.size());state->observed_analog=!output.empty();state->observed_corrected_keys=movement_buttons(previous);state->observed_move_number=state->observed_command;
}

void safe_prepare_movement(Address services,Address command) {
    try {
            capture_direction_input(command);
            const auto cmd=read_native<Command>(command);const auto config=movement_settings.load();
            const auto controller=read_native<Address>(state->client+cosmetics::bindings::local_controller);
            const auto handle=controller?read_native<std::uint32_t>(controller+state->f.pawn):0xffffffffu;
            const auto pawn=sdk::entity(state->client,handle);
            if(config&&config->active()&&!round_frozen&&cmd.base&&pawn&&read_native<int>(pawn+state->f.health)>0&&read_native<Address>(pawn+state->f.movement)==services){
                const auto base=read_native<Base>(cmd.base);const auto scene=read_native<Address>(pawn+state->f.scene);
                const bool known=std::any_of(state->movement_observations.begin(),state->movement_observations.end(),[&](const auto& sample){return sample.number==cmd.number&&sample.tick==base.tick&&sample.seed==base.random_seed&&sample.pawn==handle;});
                if(!known&&scene){state->movement_observations.push_back({cmd.number,base.tick,base.random_seed,handle,(read_native<std::uint32_t>(pawn+state->f.flags)&1)!=0,read_native<Vector>(scene+state->f.origin),read_native<Vector>(pawn+state->f.velocity),read_native<float>(services+state->f.duck)});if(state->movement_observations.size()>150)state->movement_observations.pop_front();}
                movement_command(command);plan_jumps(command,*config,(cmd.value&2)!=0||(GetAsyncKeyState(VK_SPACE)&0x8000)!=0);
            }
    }catch(const std::exception& e){suspend(e.what());}
}
void guarded_prepare_movement(Address services,Address command) {
    __try {state->native_stage="movement before prediction";safe_prepare_movement(services,command);}
    __except(report_native_fault(GetExceptionInformation())){}
}
