#include "stdafx.h"
#include "ElementalWeapon.h"
#include "Client.h"
#include "QuestHook.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace {

// --- Elemental bonus storage (updated by server config packet 0x1006) ---
// Values are in hundredths: 200 = +100%, 50 = elemental mismatch penalty
static short g_fireBonus = 0;       // incRMAF
static short g_poisonBonus = 0;     // incRMAS
static short g_iceBonus = 0;        // incRMAI
static short g_lightningBonus = 0;  // incRMAL
static short g_elemDefault = 0;     // elemDefault
static std::mutex g_bonusMutex;

// Set by TryApplyElementalBonus for diagnostics.
static char g_lastSkillElem = 0;

// --- Skill ID → element character mapping ---
// F = Fire, S = Poison, I = Ice, L = Lightning
// Covers all magic skills with elemental attributes.
static std::unordered_map<int, char> BuildSkillElementMap() {
    std::unordered_map<int, char> m;

    // === CHINESE WZ SKILL IDs (verified 2026-07-04) ===
    // Fire/Poison Wizard
    m[2101004] = 'F'; // 火焰箭
    m[2101005] = 'S'; // 毒雾术

    // Fire/Poison Mage
    m[2111002] = 'F'; // 末日烈焰
    m[2111003] = 'S'; // 致命毒雾
    m[2111006] = 'F'; // 火毒合击

    // Fire/Poison ArchMage
    m[2121003] = 'F'; // 火凤球
    m[2121005] = 'S'; // 冰破魔兽
    m[2121007] = 'F'; // 天降落星

    // Ice/Lightning Wizard
    m[2201004] = 'I'; // 冰冻术
    m[2201005] = 'L'; // 雷电术

    // Ice/Lightning Mage
    m[2211002] = 'I'; // 冰咆哮
    m[2211003] = 'L'; // 落雷枪
    m[2211006] = 'I'; // 冰雷合击

    // Ice/Lightning ArchMage
    m[2221003] = 'I'; // 冰凤球
    m[2221006] = 'L'; // 链环闪电
    m[2221007] = 'I'; // 落霜冰破

    // Cleric/Priest/Bishop
    m[2301005] = 'H'; // 圣箭术
    m[2311004] = 'H'; // 圣光
    m[2321007] = 'H'; // 光芒飞箭

    // Blaze Wizard (Chinese WZ)
    m[12001004] = 'F'; // 炎精灵
    m[12111003] = 'F'; // 天降落星(BW)
    m[12111004] = 'F'; // 火魔兽(BW)
    m[12111005] = 'F'; // 火牢术屏障
    m[12111006] = 'F'; // 火风暴

    return m;
}

static const std::unordered_map<int, char> kSkillElementMap = BuildSkillElementMap();

// --- Utility ---

static unsigned short ReadU16(const unsigned char* ptr) {
    return static_cast<unsigned short>(ptr[0] | (ptr[1] << 8));
}

static int ReadI32(const unsigned char* ptr) {
    return static_cast<int>(
        static_cast<unsigned int>(ptr[0])
        | (static_cast<unsigned int>(ptr[1]) << 8)
        | (static_cast<unsigned int>(ptr[2]) << 16)
        | (static_cast<unsigned int>(ptr[3]) << 24));
}

static void WriteI32(unsigned char* ptr, int value) {
    ptr[0] = static_cast<unsigned char>(value & 0xFF);
    ptr[1] = static_cast<unsigned char>((value >> 8) & 0xFF);
    ptr[2] = static_cast<unsigned char>((value >> 16) & 0xFF);
    ptr[3] = static_cast<unsigned char>((value >> 24) & 0xFF);
}

static short GetBonusForElement(char elem) {
    switch (elem) {
    case 'F': return g_fireBonus;
    case 'S': return g_poisonBonus;
    case 'I': return g_iceBonus;
    case 'L': return g_lightningBonus;
    case 'H': return 0; // Holy — no weapon with incRMAH exists
    default:  return 0;
    }
}

// Kept as a no-op while native client WZ elemental calculation is authoritative.
extern "C" int __cdecl ApplyCalcDamageElementalBonus(int damage) {
    return damage;
}

} // anonymous namespace

// --- Public API ---

namespace ElementalWeapon {

void HandleConfigPacket(const unsigned char* payload, unsigned long payloadSize) {
    if (payloadSize < 10) {
        return; // Minimum: 5 shorts = 10 bytes
    }

    short fireBonus = static_cast<short>(ReadU16(payload));
    short poisonBonus = static_cast<short>(ReadU16(payload + 2));
    short iceBonus = static_cast<short>(ReadU16(payload + 4));
    short lightningBonus = static_cast<short>(ReadU16(payload + 6));
    short elemDefault = static_cast<short>(ReadU16(payload + 8));

    {
        std::lock_guard<std::mutex> lock(g_bonusMutex);
        g_fireBonus = fireBonus;
        g_poisonBonus = poisonBonus;
        g_iceBonus = iceBonus;
        g_lightningBonus = lightningBonus;
        g_elemDefault = elemDefault;
    }

    if (Client::debug) {
        QuestHookTrace("ElementalWeapon config: F=%d S=%d I=%d L=%d elemDefault=%d",
            fireBonus, poisonBonus, iceBonus, lightningBonus, elemDefault);
    }
}

bool TryApplyElementalBonus(COutPacket* packet) {
    if (packet == nullptr || packet->Data == nullptr || packet->Size < 29) {
        return false;
    }

    const unsigned char* data = packet->Data;
    const unsigned short opcode = ReadU16(data);
    if (opcode != 0x002E) {
        return false; // Not a magic attack
    }

    const unsigned char attackedAndDamage = data[3];
    const int numAttacked = (attackedAndDamage >> 4) & 0x0F;
    const int numDamage = attackedAndDamage & 0x0F;

    if (numAttacked == 0 || numDamage == 0) {
        return false;
    }

    const int skillId = ReadI32(data + 4);

    // Look up skill element
    auto it = kSkillElementMap.find(skillId);
    if (it == kSkillElementMap.end()) {
        g_lastSkillElem = 0; // Not an elemental skill
        return false;
    }

    const char elemChar = it->second;
    g_lastSkillElem = elemChar;

    short bonus;
    short elemDefault;
    {
        std::lock_guard<std::mutex> lock(g_bonusMutex);
        bonus = GetBonusForElement(elemChar);
        elemDefault = g_elemDefault;
    }

    // Fixed damage offset: header(25) + oid(4) + skip14 = 43
    const int DMG_OFFSET = 43;
    int firstRawDmg = 0;
    if (packet->Size >= DMG_OFFSET + 4) {
        firstRawDmg = ReadI32(packet->Data + DMG_OFFSET);
    }

    if (Client::debug) {
        const short nativeRate = bonus > 0 ? bonus : elemDefault;
        QuestHookTrace("[DMG] Native skill=%d e=%c dmg=%d rate=%d F=%d S=%d I=%d L=%d elemDefault=%d",
            skillId, elemChar, firstRawDmg, nativeRate,
            g_fireBonus, g_poisonBonus, g_iceBonus, g_lightningBonus, g_elemDefault);
    }

    return false;
}

} // namespace ElementalWeapon
