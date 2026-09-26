#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>

namespace oju
{
/*  Beat Link: how OJU instances talk to each other with no routing.

    Every OJU in the host process shares one LinkHub (juce::SharedResourcePointer).
    Each instance claims a slot and publishes a few numbers through atomics - no locks,
    no allocation, safe from the audio thread:
      - an OJU on the beat (role Beat) publishes the key it hears,
      - an OJU on the vocal publishes how much the vocal is singing right now and the
        frequencies where the voice lives, so the beat can make room (Carve).
    Instances pair up by link group (A-D). Works whenever the host runs its plugins in
    one process (Reaper's default, and every major DAW).                               */
class LinkHub
{
public:
    static constexpr int maxSlots = 64;
    static constexpr int numFocus = 3;

    struct Slot
    {
        std::atomic<int> used { 0 };
        std::atomic<int> role { 0 }, group { 0 };
        std::atomic<juce::int64> heartbeat { 0 };        // ms, written by the message thread

        // Beat -> vocal
        std::atomic<int> key { -1 };
        std::atomic<int> keyMinor { 1 };
        std::atomic<float> keyConfidence { 0.0f };

        // Vocal -> beat
        std::atomic<float> vocalActivity { 0.0f };       // 0..1, how much the vocal is singing now
        std::array<std::atomic<float>, numFocus> focusHz;
        std::array<std::atomic<float>, numFocus> focusWeight;

        Slot()
        {
            const float hz[numFocus] = { 300.0f, 2500.0f, 5000.0f };
            const float w[numFocus] = { 0.5f, 1.0f, 0.6f };
            for (int i = 0; i < numFocus; ++i) { focusHz[(size_t) i] = hz[i]; focusWeight[(size_t) i] = w[i]; }
        }
    };

    int acquire() noexcept
    {
        for (int i = 0; i < maxSlots; ++i)
        {
            int expected = 0;
            if (slots[(size_t) i].used.compare_exchange_strong (expected, 1))
            {
                auto& s = slots[(size_t) i];
                s.key = -1; s.keyConfidence = 0.0f; s.vocalActivity = 0.0f;
                s.heartbeat = juce::Time::currentTimeMillis();
                return i;
            }
        }
        return -1;
    }

    void release (int index) noexcept
    {
        if (index >= 0 && index < maxSlots)
        {
            auto& s = slots[(size_t) index];
            s.vocalActivity = 0.0f;
            s.key = -1;
            s.used = 0;
        }
    }

    Slot& slot (int index) noexcept { return slots[(size_t) index]; }

    bool alive (int index, juce::int64 now) const noexcept
    {
        const auto& s = slots[(size_t) index];
        return s.used.load() != 0 && now - s.heartbeat.load() < 2000;
    }

    // Best key from a beat in this group (message thread).
    bool beatKey (int group, int self, int& key, bool& minor, float& confidence) const noexcept
    {
        const auto now = juce::Time::currentTimeMillis();
        confidence = 0.0f; key = -1;
        for (int i = 0; i < maxSlots; ++i)
        {
            if (i == self || ! alive (i, now)) continue;
            const auto& s = slots[(size_t) i];
            if (s.role.load() != 1 || s.group.load() != group || s.key.load() < 0) continue;
            if (s.keyConfidence.load() > confidence)
            {
                confidence = s.keyConfidence.load();
                key = s.key.load();
                minor = s.keyMinor.load() != 0;
            }
        }
        return key >= 0;
    }

    int count (int group, int role, int self) const noexcept
    {
        const auto now = juce::Time::currentTimeMillis();
        int n = 0;
        for (int i = 0; i < maxSlots; ++i)
            if (i != self && alive (i, now) && slots[(size_t) i].role.load() == role && slots[(size_t) i].group.load() == group)
                ++n;
        return n;
    }

    // What the beat should make room for (audio thread safe: atomics only, no clock).
    float vocalFocus (int group, int self, std::array<float, numFocus>& hz, std::array<float, numFocus>& weight) const noexcept
    {
        float activity = 0.0f;
        for (int i = 0; i < maxSlots; ++i)
        {
            if (i == self) continue;
            const auto& s = slots[(size_t) i];
            if (s.used.load (std::memory_order_relaxed) == 0 || s.role.load (std::memory_order_relaxed) != 0
                || s.group.load (std::memory_order_relaxed) != group)
                continue;
            const float a = s.vocalActivity.load (std::memory_order_relaxed);
            if (a > activity)
            {
                activity = a;
                for (int f = 0; f < numFocus; ++f)
                {
                    hz[(size_t) f] = s.focusHz[(size_t) f].load (std::memory_order_relaxed);
                    weight[(size_t) f] = s.focusWeight[(size_t) f].load (std::memory_order_relaxed);
                }
            }
        }
        return activity;
    }

private:
    std::array<Slot, maxSlots> slots;
};

} // namespace oju
