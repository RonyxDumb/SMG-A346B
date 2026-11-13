#include "Game/System/SysConfigFile.hpp"
#include "Game/System/BinaryDataContentAccessor.hpp"
#include "Game/Util/MemoryUtil.hpp"

#ifdef TARGET_MOBILE
class CompletedFileSeedChunk final : public BinaryDataChunkBase {
public:
    u32 makeHeaderHashCode() const override {
        return 1;
    }

    u32 getSignature() const override {
        return 'SEED';
    }

    s32 serialize(u8* pBuffer, u32) const override {
        pBuffer[0] = mSeeded;
        return 1;
    }

    s32 deserialize(const u8* pBuffer, u32 size) override {
        mSeeded = size != 0 && pBuffer[0] != 0;
        return 0;
    }

    void initializeData() override {
        mSeeded = false;
    }

    bool mSeeded = false;
};
#endif

SysConfigChunk::SysConfigChunk() : mHeaderSerializer(nullptr) {
    initHeaderSerializer();
    initializeData();
}

u32 SysConfigChunk::makeHeaderHashCode() const {
    return 0x1;
}

u32 SysConfigChunk::getSignature() const {
    return 'SYSC';
}

s32 SysConfigChunk::deserialize(const u8* pBuffer, u32 size) {
    s32 result;
    const char* pName;

    BinaryDataContentAccessor accessor = BinaryDataContentAccessor(const_cast< u8* >(pBuffer));
    u32 headerSize = accessor.getHeaderSize();
    u8* pData = const_cast< u8* >(pBuffer) + headerSize;

    pName = "mTimeAnnounced";
    mTimeAnnounced = *static_cast< OSTime* >(accessor.getPointer(pName, pData));

    pName = "mTimeSent";
    mTimeSent = *static_cast< OSTime* >(accessor.getPointer(pName, pData));

    pName = "mSentBytes";
    mSentBytes = *static_cast< u32* >(accessor.getPointer(pName, pData));

    s32 newSize = headerSize + mHeaderSerializer->getDataSize();

    result = 3;

    if (newSize <= size) {
        result = 0;
    }

    return result;
}

void SysConfigChunk::initializeData() {
    mTimeAnnounced = 0;
    mTimeSent = 0;
    mSentBytes = 0;
}

void SysConfigChunk::initHeaderSerializer() {
    mHeaderSerializer = new BinaryDataContentHeaderSerializer(new u8[32], 32);

    mHeaderSerializer->addAttribute("mTimeAnnounced", sizeof(mTimeAnnounced));
    mHeaderSerializer->addAttribute("mTimeSent", sizeof(mTimeSent));
    mHeaderSerializer->addAttribute("mSentBytes", sizeof(mSentBytes));
    mHeaderSerializer->flush();
}

SysConfigFile::SysConfigFile() : mChunk(nullptr), mChunkHolder(nullptr) {
    mChunkHolder = new BinaryDataChunkHolder(0x3000, 8);
    mChunk = new SysConfigChunk();

    mChunkHolder->addChunk(mChunk);
#ifdef TARGET_MOBILE
    mCompletedFileSeedChunk = new CompletedFileSeedChunk();
    mChunkHolder->addChunk(mCompletedFileSeedChunk);
#endif
}

OSTime SysConfigFile::getTimeAnnounced() {
    return mChunk->mTimeAnnounced;
}

void SysConfigFile::updateTimeAnnounced() {
    SysConfigChunk* pChunk = mChunk;

    pChunk->mTimeAnnounced = OSGetTime();
}

OSTime SysConfigFile::getTimeSent() {
    return mChunk->mTimeSent;
}

void SysConfigFile::setTimeSent(OSTime timeSent) {
    mChunk->mTimeSent = timeSent;
}

u32 SysConfigFile::getSentBytes() {
    return mChunk->mSentBytes;
}

void SysConfigFile::setSentBytes(u32 sentBytes) {
    mChunk->mSentBytes = sentBytes;
}

void SysConfigFile::makeDataBinary(u8* pBuffer, u32 size) const {
    mChunkHolder->makeFileBinary(pBuffer, size);
}

void SysConfigFile::loadFromDataBinary(const u8* pBuffer, u32 size) {
#ifdef TARGET_MOBILE
    mCompletedFileSeedChunk->initializeData();
#endif
    mChunkHolder->loadFromFileBinary(pBuffer, size);
}

#ifdef TARGET_MOBILE
bool SysConfigFile::hasSeededCompletedFile() const {
    return mCompletedFileSeedChunk->mSeeded;
}

void SysConfigFile::markCompletedFileSeeded() {
    mCompletedFileSeedChunk->mSeeded = true;
}
#endif

s32 SysConfigChunk::serialize(u8* pBuffer, u32 size) const {
    void* pSrcBuffer = mHeaderSerializer->mStream.mBuffer;
    const char* pName;

    MR::copyMemory(pBuffer, pSrcBuffer, mHeaderSerializer->getHeaderSize());

    u32 headerSize = mHeaderSerializer->getHeaderSize();
    BinaryDataContentAccessor accessor = BinaryDataContentAccessor(static_cast< u8* >(mHeaderSerializer->mStream.mBuffer));
    u8* pData = pBuffer + headerSize;

    pName = "mTimeAnnounced";
    OSTime* pTimeAnnounced = static_cast< OSTime* >(accessor.getPointer(pName, pData));

    pName = "mTimeSent";
    OSTime* pTimeSent = static_cast< OSTime* >(accessor.getPointer(pName, pData));

    pName = "mSentBytes";
    u32* pSentBytes = static_cast< u32* >(accessor.getPointer(pName, pData));

    *pTimeAnnounced = mTimeAnnounced;
    *pTimeSent = mTimeSent;
    *pSentBytes = mSentBytes;

    return headerSize + mHeaderSerializer->getDataSize();
}
