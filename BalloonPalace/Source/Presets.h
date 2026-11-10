#pragma once
#include <juce_core/juce_core.h>
#include <vector>

//==============================================================================
// Factory presets. Each one has a background picture (Resources/bg_NN.jpg) and a colour
// theme taken from it. Every preset starts from the parameters' defaults, then sets its own
// values; store effects it uses are bought, the rest are left on the shelf.
namespace presets
{
struct Theme { juce::uint32 deep, accent, text; };

// one theme per background picture, pulled from the picture's colours
inline const Theme& themeFor (int bg)
{
    static const Theme themes[] = {
        { 0xff19211e, 0xff90d977, 0xffecffe6 },  // 0: Bird%27s-eye_view_of_Ghastly_Gorge_S8E19
        { 0xff171121, 0xff7f36d9, 0xfff1e6ff },  // 1: Canterlot_S1E26.webp
        { 0xff171c21, 0xffd99977, 0xffffefe6 },  // 2: Chariot_about_to_land_S4E11.webp
        { 0xff0c1a21, 0xff65b5dc, 0xffe6f7ff },  // 3: Cloudsdale1.webp
        { 0xff212112, 0xffbdd977, 0xfff8ffe6 },  // 4: Froggy_Bottom_S1E15.webp
        { 0xff132120, 0xff77d8d9, 0xffe6ffff },  // 5: Las_Pegasus_S6E20.webp
        { 0xff121a21, 0xff77a8d9, 0xffe6f3ff },  // 6: Moonlight_shining_on_the_Hollow_Shades_S
        { 0xff16211c, 0xff77d9b1, 0xffe6fff5 },  // 7: Pinkie_and_Maud_reach_the_rock_farm_S4E1
        { 0xff142121, 0xff81e8eb, 0xffe6feff },  // 8: Ponyville_as_seen_from_Twilight_and_Spik
        { 0xff162120, 0xff69c4d9, 0xffe6faff },  // 9: Rainbow_waterfalls_S3E6.webp
        { 0xff19211f, 0xffadd961, 0xfff6ffe6 },  // 10: The_lake_S5E5.webp
        { 0xff17211e, 0xff7be7b4, 0xffe6fff3 },  // 11: View_of_Rainbow_Falls_S4E10.webp
        { 0xff21210d, 0xff9ed977, 0xfff0ffe6 },  // 12: War_in_the_Smokey_Mountains_valley_S5E23
        { 0xff14211e, 0xff89e4f9, 0xffe6faff }   // 13: ponyville_background_by_lightf4lls_d5qpq
    };
    return themes[juce::jlimit (0, (int) (sizeof (themes) / sizeof (themes[0])) - 1, bg)];
}
inline constexpr int numBackgrounds = 14;

struct Value { juce::String id; float v; };
struct Preset
{
    const char* name;
    const char* group;
    int background;               // -1: the plain night-sky look
    juce::String scale;           // scale name (empty: keep the default)
    std::vector<Value> values;    // real units (choices and switches by index)
    std::vector<int> bought;      // store effects to buy (fp::StoreFX::Kind)
};

const std::vector<Preset>& list();
}
