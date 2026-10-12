#include "core/features/inventory/rarities.hpp"
#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace iris::cosmetics::inventory {
namespace {
struct Rarity {int id,value;};
constexpr Rarity definitions[]{
#define IRIS_ITEM_RARITY(id,value) {id,value},
#define IRIS_PAINT_RARITY(id,value)
#include "core/sdk/generated/inventory_rarities.inc"
#undef IRIS_ITEM_RARITY
#undef IRIS_PAINT_RARITY
};
constexpr Rarity paints[]{
#define IRIS_ITEM_RARITY(id,value)
#define IRIS_PAINT_RARITY(id,value) {id,value},
#include "core/sdk/generated/inventory_rarities.inc"
#undef IRIS_ITEM_RARITY
#undef IRIS_PAINT_RARITY
};
template<std::size_t N>int find(const Rarity(&values)[N],int id) {
    const auto row=std::lower_bound(std::begin(values),std::end(values),id,[](const auto& entry,int key){return entry.id<key;});
    if(row==std::end(values)||row->id!=id)throw std::runtime_error("Inventory rarity is absent from the reviewed installed catalog.");
    return row->value;
}
}
int item_rarity(int definition,int paint) {
    const auto base=find(definitions,definition);
    if(!paint)return base;
    const auto finish=find(paints,paint);
    // Exact client1114830, reached by the named GetItemRarity path. Contraband
    // paint rarity7 allows7; all other paints cap the combined value at6.
    return std::clamp(base+finish-1,0,finish==7?7:6);
}
}
