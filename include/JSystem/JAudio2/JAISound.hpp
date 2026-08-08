#pragma once

#include <revolution/types.h>

class JAISoundID {
public:
    enum SoundType {
        SOUND_SE = 0,
        SOUND_SEQ = 1,
        SOUND_STREAM = 2,
    };

    JAISoundID() {
    }

    JAISoundID(u32 id) {
        mID.mComposite = id;
    }

    JAISoundID(const JAISoundID& other) : mID(other.mID) {
    }

    JAISoundID(u32 section_id, u32 group_id, u32 wave_id) {
        mID.info.type.parts.sectionID = section_id;
        mID.info.type.parts.groupID = group_id;
        mID.info.waveID = wave_id;
    }

    operator u32() const {
        return mID.mComposite;
    }

    [[nodiscard]] bool isAnonymous() const {
        return mID.mComposite == static_cast<u32>(-1);
    }

    void setAnonymous() {
        mID.mComposite = static_cast<u32>(-1);
    }

    [[nodiscard]] u8 getSectionID() const {
        return mID.info.type.parts.sectionID;
    }

    void setSectionID(u8 id) {
        mID.info.type.parts.sectionID = id;
    }

    [[nodiscard]] u8 getGroupID() const {
        return mID.info.type.parts.groupID;
    }

    void setGroupID(u8 id) {
        mID.info.type.parts.groupID = id;
    }

    [[nodiscard]] u16 getWaveID() const {
        return mID.info.waveID;
    }

    void getWaveID(u16 id) {
        mID.info.waveID = id;
    }

    union {
        u32 mComposite;
        struct {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            u16 waveID;
            union {
                u16 value;
                struct {
                    u8 groupID;
                    u8 sectionID;
                } parts;
            } type;
#else
            union {
                u16 value;
                struct {
                    u8 sectionID;
                    u8 groupID;
                } parts;
            } type;
            u16 waveID;
#endif
        } info;
    } mID;
};

// Aurora can preserve logical JAudio request identity even when a platform
// backend intentionally performs silent playback.
class JAISoundHandle {
public:
    [[nodiscard]] bool isSoundAttached() const {
        return mAttached;
    }

    void attach() {
        mAttached = true;
    }

    void releaseSound() {
        mAttached = false;
    }

private:
    bool mAttached = false;
};
