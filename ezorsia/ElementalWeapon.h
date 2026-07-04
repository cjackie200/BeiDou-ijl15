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
// payload points to the byte after the 4-byte header; payloadSize is the remaining length.
void HandleConfigPacket(const unsigned char* payload, unsigned long payloadSize);

// Intercepts a magic attack packet (opcode 0x2E) and multiplies each damage
// number by the matching elemental bonus. Modifies packet data in-place.
// Returns true if the packet was modified.
bool TryApplyElementalBonus(COutPacket* packet);

} // namespace ElementalWeapon
