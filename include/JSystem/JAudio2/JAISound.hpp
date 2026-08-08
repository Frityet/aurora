#pragma once

#include <revolution/types.h>

#include <stdexcept>

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

// A PC-side JAudio handle is attached only while it identifies a concrete
// backend voice. The owner retires the handle when that voice actually ends.
class JAISoundHandle {
public:
    [[nodiscard]] bool isSoundAttached() const {
        return mAttached;
    }

    void attachBackend(const void *owner, u64 token) {
        if (owner == nullptr || token == 0U) {
            throw std::invalid_argument(
                "A JAudio handle requires a concrete backend owner and token");
        }
        mAttached = true;
        mBackendOwner = owner;
        mBackendToken = token;
    }

    [[nodiscard]] bool isBackendAttached(const void *owner, u64 token) const {
        return mAttached && mBackendOwner == owner && mBackendToken == token;
    }

    [[nodiscard]] const void *backendOwner() const {
        return mBackendOwner;
    }

    [[nodiscard]] u64 backendToken() const {
        return mBackendToken;
    }

    void releaseSound() {
        mAttached = false;
        mBackendOwner = nullptr;
        mBackendToken = 0;
    }

private:
    bool mAttached = false;
    const void *mBackendOwner = nullptr;
    u64 mBackendToken = 0;
};
