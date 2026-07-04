#pragma once

// Packet structure shared with the game client.
// Originally defined in QuestHook.cpp anonymous namespace; moved here so
// ElementalWeapon.cpp can access it.
struct COutPacket {
    int Loopback;
    union {
        unsigned char* Data;
        void* Unk;
        unsigned short* Header;
    };
    unsigned long Size;
    unsigned int Offset;
    int EncryptedByShanda;
};

namespace ElementalWeapon {

// Called when the server sends elemental weapon config (opcode 0x1006).
void HandleConfigPacket(const unsigned char* payload, unsigned long payloadSize);

// Intercepts magic attack packets (0x2E) and modifies damage numbers in-place.
// Returns true if the packet was modified.
bool TryApplyElementalBonus(COutPacket* packet);

} // namespace ElementalWeapon

// Called from CalcDamage::MDamage codecave (C linkage).
// Applies weapon elemental bonus to the damage value in EAX.
extern "C" int __cdecl ApplyCalcDamageElementalBonus(int damage);
