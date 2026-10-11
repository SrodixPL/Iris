#include "core/features/player/esp/elements.hpp"
#include <algorithm>
#include <cstdio>

namespace iris::visuals::esp {
ElementRects layout_elements(const EspLayout& input,const std::array<ElementMeasure,esp_element_count>& measures,ImVec2 min,ImVec2 max,float gap) {
    auto layout=input;normalize_esp_layout(layout);ElementRects result;
    for(unsigned side=0;side<4;++side){
        std::array<std::size_t,esp_element_count> order{};std::size_t count{};
        for(std::size_t i=0;i<layout.size();++i)if(static_cast<unsigned>(layout[i].side)==side&&measures[i].enabled)order[count++]=i;
        std::sort(order.begin(),order.begin()+count,[&](auto a,auto b){return layout[a].order<layout[b].order;});
        float offset=gap;std::array<float,4> text_y{},text_height{},text_width{};
        const auto anchor_of=[&](std::size_t id){auto anchor=layout[id].anchor;if(anchor==EspAnchor::Automatic)anchor=side<2?EspAnchor::Center:EspAnchor::Start;return static_cast<unsigned>(anchor);};
        for(std::size_t index=0;index<count;++index){const auto id=order[index];if(!measures[id].bar)text_height[anchor_of(id)]+=measures[id].size.y+gap;}
        for(std::size_t index=0;index<count;++index){const auto id=order[index];const auto& measure=measures[id];const auto size=measure.size;auto& rect=result[id];rect.drawn=true;const auto anchor=anchor_of(id);
            if(side<2){const float x=measure.bar||(anchor==2)?(min.x+max.x-size.x)*.5f:anchor==1?min.x:max.x-size.x;const float y=side==0?min.y-offset-size.y:max.y+offset;rect.min={x,y};rect.max={x+size.x,y+size.y};offset+=size.y+gap;}
            else {
                if(measure.bar){const auto width=*std::max_element(text_width.begin(),text_width.end());if(width>0){offset+=width+gap;text_width.fill(0);text_y.fill(0);}const float x=side==2?min.x-offset-size.x:max.x+offset;rect.min={x,min.y};rect.max={x+size.x,min.y+size.y};offset+=size.x+gap;}
                else {const float height=std::max(0.f,text_height[anchor]-gap);const float origin=anchor==1?min.y:anchor==2?(min.y+max.y-height)*.5f:max.y-height;const float x=side==2?min.x-offset-size.x:max.x+offset;rect.min={x,origin+text_y[anchor]};rect.max={x+size.x,origin+text_y[anchor]+size.y};text_y[anchor]+=size.y+gap;text_width[anchor]=std::max(text_width[anchor],size.x);}
            }
        }
    }
    return result;
}
ElementRects draw_elements(ImDrawList* d,const Player& p,const Settings& c,ImVec2 min,ImVec2 max,Icon (*lookup)(void*,int),void* owner) {
    const float font=ImGui::GetFontSize(),width=max.x-min.x,height=max.y-min.y;
    const auto index=[](EspElement e){return static_cast<std::size_t>(e);};
    std::array<ElementMeasure,esp_element_count> measure{};
    const auto set=[&](EspElement e,bool on,ImVec2 size,bool is_bar=false){measure[index(e)]={size,on,is_bar};};
    char hp[16]{},clip[24]{},distance[24]{};std::snprintf(hp,sizeof(hp),"%d",p.health);std::snprintf(clip,sizeof(clip),"%d",p.clip);std::snprintf(distance,sizeof(distance),"%.0f m",p.distance);
    set(EspElement::Name,c.name&&!p.name.empty(),ImGui::CalcTextSize(p.name.c_str()));
    set(EspElement::HealthNumber,c.health_number,ImGui::CalcTextSize(hp));
    set(EspElement::Distance,c.distance,ImGui::CalcTextSize(distance));
    const auto bar_size=[&](EspElement e,bool with_number){const bool vertical=static_cast<unsigned>(c.esp_layout[index(e)].side)>=2;const float label_width=with_number?ImGui::CalcTextSize(clip).x+4:0;return vertical?ImVec2{3+label_width,height}:ImVec2{width+label_width,with_number?std::max(3.f,font):3.f};};
    set(EspElement::Health,c.health,bar_size(EspElement::Health,false),true);
    set(EspElement::Ammo,c.ammo&&p.clip>=0,bar_size(EspElement::Ammo,true),true);
    set(EspElement::Armor,c.armor,bar_size(EspElement::Armor,false),true);
    const bool weapon_icon=c.weapon&&c.weapon_mode>0&&p.definition,weapon_name=c.weapon&&c.weapon_mode!=1&&!p.weapon.empty();
    const auto icon=weapon_icon&&lookup?lookup(owner,p.definition):Icon{};
    const float icon_height=font*1.5f,icon_width=icon_height*std::clamp(icon.aspect,.5f,3.f);
    auto weapon_size=weapon_name?ImGui::CalcTextSize(p.weapon.c_str()):ImVec2{};
    if(weapon_icon){weapon_size.x=std::max(weapon_size.x,icon_width);weapon_size.y+=icon_height+(weapon_name?2:0);}
    set(EspElement::Weapon,weapon_icon||weapon_name,weapon_size);
    const auto labels=flag_labels(p,c);ImVec2 flags_size{};for(std::size_t i=0;i<labels.count;++i)flags_size.x=std::max(flags_size.x,ImGui::CalcTextSize(labels.values[i].data()).x);flags_size.y=labels.count*font;
    set(EspElement::Flags,labels.count>0,flags_size);
    const auto rects=layout_elements(c.esp_layout,measure,min,max);
    const auto& base=p.enemy?(p.visible?c.enemy_color:c.hidden_color):c.ally_color;
    const auto tint_color=[&](const auto& value){return rgba(tint(base,value));};
    for(std::size_t id=0;id<rects.size();++id){const auto& rect=rects[id];if(!rect.drawn)continue;const auto e=static_cast<EspElement>(id);const bool vertical=static_cast<unsigned>(c.esp_layout[id].side)>=2;
        switch(e){
        case EspElement::Name:text(d,rect.min,rgba(c.name_color),p.name.c_str());break;
        case EspElement::HealthNumber:text(d,rect.min,rgba(c.health_number_color),hp);break;
        case EspElement::Distance:text(d,rect.min,rgba(c.distance_color),distance);break;
        case EspElement::Health:bar(d,rect.min,rect.max,std::clamp(p.health/100.f,0.f,1.f),rgba(health_color(c,p.health/100.f)),vertical);break;
        case EspElement::Armor:bar(d,rect.min,rect.max,p.armor/100.f,rgba(c.armor_color),vertical);break;
        case EspElement::Ammo:{const ImVec2 end{vertical?rect.min.x+3:rect.min.x+width,vertical?rect.max.y:rect.min.y+3};if(p.max_clip>0)bar(d,rect.min,end,static_cast<float>(p.clip)/p.max_clip,tint_color(c.ammo_color),vertical);text(d,{end.x+4,vertical?end.y-font:rect.min.y},tint_color(c.ammo_color),clip);break;}
        case EspElement::Weapon:{float y=rect.min.y;const float center=(rect.min.x+rect.max.x)*.5f;if(weapon_icon){const ImVec2 at{center-icon_width*.5f,y},end{center+icon_width*.5f,y+icon_height};if(icon.texture)d->AddImage(ImTextureRef(icon.texture),at,end,{icon.u0,icon.v0},{icon.u1,icon.v1},rgba(c.weapon_color));else{d->AddRect(at,end,rgba(c.weapon_color),2);d->AddLine({center-icon_width*.2f,y+icon_height*.5f},{center+icon_width*.2f,y+icon_height*.5f},rgba(c.weapon_color),2);}y+=icon_height+2;}if(weapon_name)text(d,{center,y},rgba(c.weapon_color),p.weapon.c_str(),true);break;}
        case EspElement::Flags:for(std::size_t i=0;i<labels.count;++i)text(d,{rect.min.x,rect.min.y+i*font},tint_color(c.flags_color),labels.values[i].data());break;
        default:break;
        }
    }
    return rects;
}
}
