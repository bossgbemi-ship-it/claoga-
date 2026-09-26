#include "Brain.h"
#include "../Parameters.h"

namespace oju
{
namespace
{
    struct StyleProfile
    {
        float presenceTarget, airTarget, bodyBias, lowCutScale;
        float ratio, grNatural, grExtreme;
        float heatDrive, heatMix;
        int   division;
        float feedback, echo, size, verb;
        float parallel, tameBias;
        const char* flavour;
    };

    //                              pres  air  body  lc    ratio grN  grE  drive mix  div fb    echo  size  verb  par   tame
    const StyleProfile profiles[] = {
        /* Afrobeats */ { 1.5f, 2.0f, 0.5f, 1.00f, 3.5f, 4.0f, 7.0f, 5.0f, 0.35f, 3, 0.28f, 0.12f, 0.45f, 0.13f, 0.30f,  0.0f, "bright and bouncy" },
        /* Trap      */ { 2.5f, 3.0f, 0.0f, 1.15f, 5.0f, 5.0f, 9.0f, 7.0f, 0.45f, 2, 0.22f, 0.14f, 0.35f, 0.10f, 0.40f,  5.0f, "upfront and aggressive" },
        /* R&B       */ { 0.5f, 2.0f, 1.5f, 0.95f, 3.0f, 3.5f, 6.0f, 3.0f, 0.30f, 4, 0.35f, 0.15f, 0.65f, 0.22f, 0.25f,  5.0f, "smooth and wide" },
        /* Pop       */ { 2.0f, 2.5f, 0.0f, 1.05f, 4.0f, 4.0f, 7.0f, 4.0f, 0.30f, 2, 0.22f, 0.10f, 0.50f, 0.14f, 0.30f,  0.0f, "polished and forward" },
        /* Soul      */ { 0.0f, 0.5f, 1.5f, 0.90f, 2.5f, 3.0f, 5.5f, 6.0f, 0.45f, 0, 0.12f, 0.14f, 0.40f, 0.15f, 0.20f, -5.0f, "warm and live" },
        // OJU 2.0 genres
        /* Rap       */ { 2.5f, 2.5f, 0.5f, 1.10f, 5.0f, 5.0f, 8.0f, 6.0f, 0.40f, 2, 0.18f, 0.10f, 0.30f, 0.08f, 0.35f,  3.0f, "tight and in your face" },
        /* Amapiano  */ { 1.5f, 3.0f, 0.5f, 1.00f, 3.5f, 4.0f, 7.0f, 4.0f, 0.30f, 3, 0.35f, 0.16f, 0.55f, 0.18f, 0.25f,  0.0f, "airy and log-drum friendly" },
        /* Gospel    */ { 1.0f, 2.0f, 1.5f, 0.90f, 3.0f, 3.5f, 6.0f, 3.0f, 0.30f, 4, 0.30f, 0.12f, 0.75f, 0.25f, 0.20f,  3.0f, "big, warm and uplifting" },
    };

    // OJU 2.0 module targets per style
    struct StyleV2
    {
        int verbType;               // 0 classic, 1 room, 2 plate, 3 hall
        float echoDuck, verbDuck;
        float breath, dbl, width;
        bool hookOnly;
        float level, tuneSpeed, humanize, rider;
    };

    //                          type echoD verbD breath dbl  width hook   level tuneS human rider
    const StyleV2 profilesV2[] = {
        /* Afrobeats */ { 2, 0.45f, 0.35f, 0.50f, 0.35f, 0.60f, true,  0.45f, 45.0f, 55.0f, 0.50f },
        /* Trap      */ { 1, 0.50f, 0.40f, 0.60f, 0.40f, 0.70f, true,  0.55f, 75.0f, 30.0f, 0.60f },
        /* R&B       */ { 3, 0.35f, 0.30f, 0.45f, 0.30f, 0.60f, false, 0.40f, 35.0f, 65.0f, 0.45f },
        /* Pop       */ { 2, 0.40f, 0.35f, 0.50f, 0.35f, 0.60f, true,  0.50f, 55.0f, 45.0f, 0.50f },
        /* Soul      */ { 1, 0.30f, 0.25f, 0.30f, 0.20f, 0.40f, false, 0.35f, 20.0f, 80.0f, 0.40f },
        /* Rap       */ { 1, 0.55f, 0.45f, 0.65f, 0.45f, 0.75f, true,  0.60f, 70.0f, 35.0f, 0.60f },
        /* Amapiano  */ { 2, 0.50f, 0.40f, 0.50f, 0.40f, 0.80f, true,  0.45f, 50.0f, 50.0f, 0.50f },
        /* Gospel    */ { 3, 0.35f, 0.30f, 0.35f, 0.30f, 0.60f, false, 0.40f, 25.0f, 75.0f, 0.45f },
    };

    const char* voiceType (float lowestHz)
    {
        if (lowestHz < 95.0f)  return "Bass range";
        if (lowestHz < 125.0f) return "Baritone range";
        if (lowestHz < 160.0f) return "Tenor range";
        if (lowestHz < 200.0f) return "Alto range";
        return "Soprano range";
    }

    float roundTo (float v, float step) { return std::round (v / step) * step; }

    juce::String dash() { return juce::String::fromUTF8 (" \xe2\x80\x94 "); }

    juce::String hz (float f)
    {
        if (f >= 1000.0f)
            return juce::String (f / 1000.0f, 1) + " kHz";
        return juce::String (juce::roundToInt (f / 10.0f) * 10) + " Hz";
    }

    juce::String db (float v) { return juce::String (std::abs (roundTo (v, 0.5f)), 1) + " dB"; }

    juce::String ratioText (float r)
    {
        const float rr = roundTo (r, 0.5f);
        return (std::abs (rr - std::round (rr)) < 0.01f ? juce::String (juce::roundToInt (rr)) : juce::String (rr, 1)) + ":1";
    }

    struct Line { float weight; juce::String text; };
}

BrainResult decide (const Features& f, int styleIndex, int mode, double bpm)
{
    styleIndex = juce::jlimit (0, (int) std::size (profiles) - 1, styleIndex);
    const auto& p = profiles[styleIndex];
    const auto& p2 = profilesV2[styleIndex];
    const bool extreme = mode == 1;
    const float strength = extreme ? 1.0f : 0.7f;
    bpm = juce::jlimit (40.0, 240.0, bpm > 0.0 ? bpm : 120.0);

    BrainResult out;
    auto set = [&out] (const char* id, float v) { out.values.emplace_back (id, v); };
    std::vector<Line> lines;

    // ---- Gain staging: land the sung level around -18 dBFS RMS
    const float inputGain = juce::jlimit (-18.0f, 18.0f, roundTo (-18.0f - f.rmsP50, 0.5f));
    set (ids::inputGain, inputGain);
    if (inputGain > 4.0f)
        lines.push_back ({ 6.0f, "Sitting low at " + juce::String (juce::roundToInt (f.rmsP50)) + " dB" + dash() + "lifted input " + db (inputGain) });
    else if (inputGain < -4.0f)
        lines.push_back ({ 6.0f, "Hot input at " + juce::String (juce::roundToInt (f.rmsP50)) + " dB" + dash() + "pulled back " + db (inputGain) });
    else
        lines.push_back ({ 2.0f, "Healthy level" + dash() + "input trimmed " + (inputGain >= 0.0f ? "+" : "-") + db (inputGain) });

    // ---- Low cut: under the voice, higher if there is rumble
    float lowCut = juce::jlimit (50.0f, 130.0f, f.lowestVoiceHz * 0.6f) * p.lowCutScale;
    if (f.rumbleDb > 3.0f)
        lowCut = juce::jmax (lowCut, 90.0f);
    lowCut = juce::jlimit (40.0f, 180.0f, roundTo (lowCut, 5.0f));
    set (ids::lowCutOn, 1.0f);
    set (ids::lowCutFreq, lowCut);
    if (f.rumbleDb > 3.0f)
        lines.push_back ({ 4.0f + f.rumbleDb * 0.3f, "Rumble under 70 Hz" + dash() + "low cut at " + hz (lowCut) });
    else
        lines.push_back ({ 2.2f, juce::String (voiceType (f.lowestVoiceHz)) + dash() + "high-pass at " + hz (lowCut) + ", just under your voice" });

    // ---- Mud / box: one moveable cut aimed at whichever is worse
    const bool boxWins = f.boxExcessDb > f.mudExcessDb + 0.5f;
    const float problem = boxWins ? f.boxExcessDb : f.mudExcessDb;
    const float problemFreq = boxWins ? f.boxFreq : f.mudFreq;
    float mudCut = 0.0f;
    if (problem > 1.0f)
        mudCut = -juce::jlimit (0.0f, extreme ? 9.0f : 6.0f, (problem - 0.5f) * 0.9f * strength + 0.5f);
    mudCut = roundTo (mudCut, 0.5f);
    set (ids::eqOn, 1.0f);
    set (ids::mudFreq, juce::jlimit (120.0f, 1000.0f, roundTo (problemFreq, 5.0f)));
    set (ids::mudGain, mudCut);

    float body = p.bodyBias * strength;
    if (! boxWins && f.mudExcessDb > 3.0f)
        body -= 1.0f;
    if (f.mudExcessDb < -1.0f && f.lowestVoiceHz < 200.0f)
        body += 1.0f;
    body = roundTo (juce::jlimit (-4.0f, 4.0f, body), 0.5f);
    set (ids::bodyGain, body);

    if (mudCut < -0.4f)
        lines.push_back ({ 5.0f + problem, juce::String (boxWins ? "Boxy" : "Muddy") + " around " + hz (problemFreq) + dash() + "cut " + db (mudCut) });
    else if (body > 0.4f)
        lines.push_back ({ 2.0f, "Lows are clear" + dash() + "Body +" + db (body) + " for weight" });
    else
        lines.push_back ({ 1.5f, "Low mids are clean" + dash() + "no mud cut needed" });

    // ---- Presence and harshness
    const bool harsh = f.harshExcessDb > 2.5f;
    float presence = (p.presenceTarget - f.presenceDb) * strength;
    if (harsh)
        presence = presence * 0.4f - (f.harshExcessDb - 2.5f) * 0.6f * strength;
    presence = roundTo (juce::jlimit (-5.0f, extreme ? 7.0f : 5.0f, presence), 0.5f);
    float presenceFreq = styleIndex == 4 ? 3000.0f : (styleIndex == 2 ? 3400.0f : 4000.0f);
    if (harsh)
        presenceFreq = presence < 0.0f ? f.harshFreq : juce::jlimit (1500.0f, 6000.0f, f.harshFreq * 1.35f);
    set (ids::presenceFreq, roundTo (presenceFreq, 50.0f));
    set (ids::presenceGain, presence);

    if (harsh)
    {
        juce::String move = "presence held flat";
        if (presence > 0.4f)       move = "presence +" + db (presence) + " moved up to " + hz (presenceFreq);
        else if (presence < -0.4f) move = "presence cut " + db (presence) + " there";
        lines.push_back ({ 4.0f + f.harshExcessDb, "Harsh edge at " + hz (f.harshFreq) + dash() + move });
    }
    else if (presence > 1.4f)
        lines.push_back ({ 3.0f + presence * 0.5f, "Sitting back in the mix" + dash() + "presence +" + db (presence) + " at " + hz (presenceFreq) });
    else if (presence < -1.4f)
        lines.push_back ({ 3.0f - presence * 0.5f, "Forward and pokey" + dash() + "presence down " + db (presence) });

    // ---- Sibilance -> Tame
    float tame = (extreme ? 35.0f : 30.0f) + f.sibilanceDb * (extreme ? 5.0f : 4.5f) + p.tameBias;
    tame = roundTo (juce::jlimit (8.0f, 90.0f, tame), 1.0f);
    const float tameFreq = roundTo (juce::jlimit (3500.0f, 10000.0f, f.sibilanceFreq * 0.82f), 50.0f);
    set (ids::tameOn, 1.0f);
    set (ids::tameFreq, tameFreq);
    set (ids::tameAmount, tame);
    if (f.sibilanceDb > 3.0f)
        lines.push_back ({ 5.0f + f.sibilanceDb * 0.5f, "Sharp S's at " + hz (f.sibilanceFreq) + dash() + "Tame set to " + juce::String ((int) tame) + "%" });
    else
        lines.push_back ({ 2.5f, "Soft S's around " + hz (f.sibilanceFreq) + dash() + "Tame kept light at " + juce::String ((int) tame) + "%" });

    // ---- Air
    float air = (p.airTarget - f.airDb) * strength;
    if (f.sibilanceDb > 5.0f)
        air -= 1.0f;
    air = roundTo (juce::jlimit (-3.0f, extreme ? 8.0f : 5.0f, air), 0.5f);
    set (ids::airGain, air);
    if (air >= 2.0f)
        lines.push_back ({ 2.5f + air * 0.4f, "A little dull up top" + dash() + "Air +" + db (air) });
    else if (air <= -1.0f)
        lines.push_back ({ 2.5f, "Plenty of top end" + dash() + "Air eased " + db (air) });

    // ---- Press: ratio from dynamics, threshold from level percentiles
    float ratio = p.ratio;
    if (f.dynamicsDb > 18.0f)      ratio += 1.0f;
    else if (f.dynamicsDb < 10.0f) ratio -= 1.0f;
    if (extreme) ratio += 1.0f;
    ratio = roundTo (juce::jlimit (1.5f, 10.0f, ratio), 0.5f);

    const float gr = extreme ? p.grExtreme : p.grNatural;
    const float loud = -18.0f + juce::jlimit (2.0f, 16.0f, f.rmsP90 - f.rmsP50);
    const float threshold = roundTo (juce::jlimit (-50.0f, -6.0f, loud - gr / (1.0f - 1.0f / ratio)), 0.5f);
    const float attack = f.crestDb > 18.0f ? 3.0f : (f.crestDb > 13.0f ? 8.0f : 15.0f) * (extreme ? 0.7f : 1.0f);
    const float release = roundTo (juce::jlimit (50.0f, 250.0f, (float) (60000.0 / bpm / 4.0)), 5.0f);
    const float makeupDb = roundTo (gr * (extreme ? 0.8f : 0.65f), 0.5f);

    set (ids::pressOn, 1.0f);
    set (ids::pressRatio, ratio);
    set (ids::pressThreshold, threshold);
    set (ids::pressAttack, roundTo (attack, 0.5f));
    set (ids::pressRelease, release);
    set (ids::pressMakeup, makeupDb);
    set (ids::pressParallel, extreme ? p.parallel * 100.0f : 0.0f);

    if (f.dynamicsDb > 16.0f)
        lines.push_back ({ 5.0f, "Wide dynamics" + dash() + "Press at " + ratioText (ratio) });
    else if (f.dynamicsDb < 10.0f)
        lines.push_back ({ 3.5f, "Already steady" + dash() + "Press at a gentle " + ratioText (ratio) });
    else
        lines.push_back ({ 4.0f, "Natural movement" + dash() + "Press at " + ratioText (ratio) + ", " + db (gr) + " on the loud notes" });

    if (extreme)
        lines.push_back ({ 3.0f, "Extreme" + dash() + "parallel Press blended at " + juce::String (juce::roundToInt (p.parallel * 100.0f)) + "% for density" });

    // ---- Heat
    float drive = p.heatDrive + (extreme ? 3.0f : 0.0f);
    if (f.crestDb < 12.0f)
        drive -= 2.0f;
    set (ids::heatOn, 1.0f);
    set (ids::heatDrive, roundTo (juce::jlimit (0.0f, 18.0f, drive), 0.5f));
    set (ids::heatMix, roundTo ((p.heatMix + (extreme ? 0.1f : 0.0f)) * 100.0f, 1.0f));

    // ---- Space: tempo-aware
    int division = p.division;
    if (division == 4 && bpm > 140.0) division = 2;
    if (division == 2 && bpm < 75.0)  division = 3;
    set (ids::spaceOn, 1.0f);
    set (ids::delayTime, (float) division);
    set (ids::delayFeedback, p.feedback * 100.0f);
    set (ids::delayMix, roundTo (p.echo * (extreme ? 1.2f : 1.0f) * 100.0f, 1.0f));
    set (ids::reverbSize, p.size * 100.0f);
    set (ids::reverbMix, roundTo (p.verb * (extreme ? 1.15f : 1.0f) * 100.0f, 1.0f));
    lines.push_back ({ 1.8f, "Space on " + delayDivisionNames()[division] + " at " + juce::String (juce::roundToInt (bpm)) + " BPM, "
                             + verbTypeNames()[p2.verbType].toLowerCase() + " verb that blooms in the gaps" + dash() + p.flavour });

    // ---- Master
    set (ids::amount, 100.0f);
    set (ids::outputGain, 0.0f);
    set (ids::ceilingOn, 1.0f);

    //--------------------------------------------------------------------------
    // OJU 2.0: Auto now sets all 12 modules
    set (ids::plosiveOn, 1.0f);
    set (ids::plosiveAmount, extreme ? 70.0f : 50.0f);
    set (ids::deessAuto, 1.0f);

    set (ids::levelOn, 1.0f);
    set (ids::levelAmount, roundTo (p2.level * 100.0f * (extreme ? 1.2f : 1.0f), 1.0f));

    set (ids::breathOn, 1.0f);
    set (ids::breathAmount, roundTo (p2.breath * 100.0f * (extreme ? 1.25f : 1.0f), 1.0f));

    set (ids::doubleOn, 1.0f);
    set (ids::doubleAmount, roundTo (p2.dbl * 100.0f * (extreme ? 1.2f : 1.0f), 1.0f));
    set (ids::width, roundTo (p2.width * 100.0f, 1.0f));
    set (ids::hookOnly, p2.hookOnly ? 1.0f : 0.0f);

    set (ids::echoOn, 1.0f);
    set (ids::verbOn, 1.0f);
    set (ids::echoDuck, roundTo (p2.echoDuck * 100.0f, 1.0f));
    set (ids::verbType, (float) p2.verbType);
    set (ids::verbDuck, roundTo (p2.verbDuck * 100.0f, 1.0f));

    set (ids::riderOn, 1.0f);
    set (ids::riderAmount, roundTo (p2.rider * 100.0f, 1.0f));
    set (ids::limiterOn, 1.0f);
    set (ids::limiterCeiling, -1.0f);

    set (ids::tuneSpeed, extreme ? juce::jmin (100.0f, p2.tuneSpeed + 20.0f) : p2.tuneSpeed);
    set (ids::tuneHumanize, extreme ? juce::jmax (0.0f, p2.humanize - 20.0f) : p2.humanize);

    // ---- Keep the 6 most important lines, in a natural reading order
    std::vector<size_t> order (lines.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort (order.begin(), order.end(), [&] (size_t a, size_t b) { return lines[a].weight > lines[b].weight; });
    order.resize (std::min<size_t> (6, order.size()));
    std::sort (order.begin(), order.end());
    for (auto i : order)
        out.read.add (lines[i].text);

    // ---- One idea for the artist
    if (! extreme)
        out.idea = "Try Extreme on the ad-libs";
    else if (f.dynamicsDb > 18.0f)
        out.idea = "Ride the loudest lines a touch further from the mic";
    else if (harsh)
        out.idea = "Angle the mic slightly off-axis to soften that edge";
    else if (f.sibilanceDb > 6.0f)
        out.idea = "Try a pop filter an inch further out for softer S's";
    else if (styleIndex == 1)
        out.idea = "Stack a double 8 dB under the lead and let Heat glue them";
    else if (styleIndex == 2)
        out.idea = "Push the Verb on the last word of each phrase";
    else
        out.idea = "Keep the take, the chain is doing the rest";

    return out;
}

} // namespace oju
