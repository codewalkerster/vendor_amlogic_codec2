/*
 * Copyright (C) 2023 Amlogic, Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_NDEBUG 0
#define LOG_TAG "Img"
#include <libyuv.h>
#include <dlfcn.h>
#include <cutils/properties.h>
#include <media/stagefright/foundation/MediaDefs.h>
#include <C2Config.h>
#include <C2PlatformSupport.h>
#include <Codec2BufferUtils.h>
#include <Codec2CommonUtils.h>
#include <Codec2Mapper.h>
#include <hardware/gralloc1.h>
#include <UltraHdrHelper.h>
#include <C2VendorProperty.h>
#include <C2VendorDebug.h>
#include <C2SoftImagedec.h>
#include <C2SoftImageInterfaceImpl.h>
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#define MAX_WORK_PENDING_COUNT (7)
#ifndef UNUSED
#define UNUSED(expr)  \
    do {              \
        (void)(expr); \
    } while (0)
#endif
#define align_buffer_page_end(var, size)                                \
  uint8_t* var##_mem =                                                  \
      reinterpret_cast<uint8_t*>(malloc(((size) + 4095 + 63) & ~4095)); \
  uint8_t* var = reinterpret_cast<uint8_t*>(                            \
      (intptr_t)(var##_mem + (((size) + 4095 + 63) & ~4095) - (size)) & ~63)

#define free_aligned_buffer_page_end(var) \
  free(var##_mem);                        \
  var = 0
uint32_t android::C2Imagedec::mDumpFileCnt = 0;

namespace android {

// static
std::atomic<int32_t> C2Imagedec::sConcurrentInstances = 0;

libyuv::MJpegDecoder decoder;
// static
std::shared_ptr<C2Component> C2Imagedec::create(
        const std::string& name, c2_node_id_t id, const std::shared_ptr<IntfImpl> &intfImpl) {
    static const int32_t kMaxConcurrentInstances =2;
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    if (kMaxConcurrentInstances >= 0 && sConcurrentInstances.load() >= kMaxConcurrentInstances) {
        ALOGW("Reject to Initialize() due to too many instances: %d", sConcurrentInstances.load());
        return nullptr;
    }
    return std::shared_ptr<C2Component>(new C2Imagedec(name, id, intfImpl));
}
struct NV21Buffers {
    uint8_t* y;
    int y_stride;
    uint8_t* vu;
    int vu_stride;
    int w;
    int h;
};
static std::string uint8ArrayToHexString(const uint8_t arr[], size_t length) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < length; ++i) {
        ss << std::setw(2) << static_cast<int>(arr[i]);
    }
    return ss.str();
}
static void JpegI444ToNV21(void* opaque, const uint8_t* const* data,
               const int* strides,int rows) {
    NV21Buffers* dest = (NV21Buffers*)(opaque);
    libyuv::I444ToNV21(data[0], strides[0], data[1], strides[1], data[2], strides[2],
               dest->y, dest->y_stride, dest->vu, dest->vu_stride, dest->w, rows);
    dest->y += rows * dest->y_stride;
    dest->vu += ((rows + 1) >> 1) * dest->vu_stride;
    dest->h -= rows;
}
static int JPEG2NV21(libyuv::MJpegDecoder& jpegdecoder ,const uint8_t* src_mjpg,
               size_t src_size_mjpg,
               uint8_t* dst_y,
               int dst_stride_y,
               uint8_t* dst_vu,
               int dst_stride_vu,
               int src_width,
               int src_height,
               int dst_width,
               int dst_height) {
  NV21Buffers bufs = {dst_y,         dst_stride_y, dst_vu,
                        dst_stride_vu, dst_width,    dst_height};
  int ret = jpegdecoder.DecodeToCallback(JpegI444ToNV21, &bufs, dst_width,
                                           dst_height);
  return ret ? 0 : 1;
 }
C2Imagedec::C2Imagedec(C2String name, c2_node_id_t id,
                               const std::shared_ptr<IntfImpl> &intfImpl)
        : C2SoftImageComponent(std::make_shared<SimpleInterface<IntfImpl>>(name.c_str(), id, intfImpl)),
        mIntfImpl(intfImpl),
        mDecoderName(name),
        mWidth(320),
        mHeight(240),
        mTotalDroppedOutputFrameNum(0),
        mTotalProcessedFrameNum(0),
        mOutIndex(0u),
        mSignaledOutputEos(false),
        mSignaledError(false),
        mFirstPictureReviced(false),
        mDecInit(false),
        mExtraData(NULL),
        mDumpYuvFp(NULL),
        mDisplayRatio(0) {
        sConcurrentInstances.fetch_add(1, std::memory_order_relaxed);

        CODEC2_LOG(CODEC2_LOG_INFO, "Create %s(%s)", __func__, name.c_str());

        propGetInt(CODEC2_VDEC_LOGDEBUG_PROPERTY, &gloglevel);
        bool dumpYuvEnable = property_get_bool(C2_PROPERTY_SOFTVDEC_DUMP_YUV, false);
        if (dumpYuvEnable) {
            char pathFile[1024] = { '\0'  };
            sprintf(pathFile, "/data/tmp/codec2_%d.yuv", mDumpFileCnt++);
            mDumpYuvFp = fopen(pathFile, "wb");
            if (mDumpYuvFp) {
                CODEC2_LOG(CODEC2_LOG_INFO, "Open file %s", pathFile);
            } else {
                CODEC2_LOG(CODEC2_LOG_ERR, "Open file %s error:%s", pathFile, strerror(errno));
            }
        }

        mDisPlayByVpp = true;
}

C2Imagedec::~C2Imagedec() {
    CODEC2_LOG(CODEC2_LOG_INFO, "%s", __func__);
    mPendingWorkFrameIndexes.clear();
    onRelease();
    if (mExtraData) {
        free(mExtraData);
        mExtraData = NULL;
    }
    if (mDumpYuvFp) {
        fclose(mDumpYuvFp);
    }
    sConcurrentInstances.fetch_sub(1, std::memory_order_relaxed);
    //coverity[Error handling issues]
    CODEC2_LOG(CODEC2_LOG_INFO, "%s done", __func__);
}

c2_status_t C2Imagedec::onInit() {
    // Update width and height from Config
    CODEC2_LOG(CODEC2_LOG_INFO, "%s", __func__);
    mSize = mIntfImpl->getSize_l();
    if (mSize->width != 0 && mSize->height != 0) {
        mWidth = mSize->width;
        mHeight = mSize->height;
        CODEC2_LOG(CODEC2_LOG_INFO, "Set mWidth=%d, mHeight=%d from CCodecConfig", mWidth, mHeight);
    }
    CODEC2_LOG(CODEC2_LOG_INFO, "before getprop");
    bool support_4k = property_get_bool(PROPERTY_PLATFORM_SUPPORT_4K, true);
    bool support_8k = property_get_bool(PROPERTY_PLATFORM_SUPPORT_8K, false);
    CODEC2_LOG(CODEC2_LOG_INFO, "after getprop %d %d",support_4k,support_8k);
    if (support_4k && !support_8k) {
        mMXWidth = 3840;
        mMXHeight = 2160;
    }else if (support_8k){
        mMXWidth = 3680;
        mMXHeight = 4320;
    }else {
        mMXWidth = 1920;
        mMXHeight = 1080;
    }
    mDisplayAdapter = meson::DisplayAdapterCreateRemote();
    if (mDisplayAdapter != nullptr) {
        ConnectorType type = meson::DisplayAdapter::CONN_TYPE_UNKNOWN;
        mDisplayAdapter->getConnectorType(0, type);
        mDisplayAdapter->getHdrSdrRatio(mDisplayRatio, type);
        CODEC2_LOG(CODEC2_LOG_INFO,"display ratio %f",mDisplayRatio);
    }
    CODEC2_LOG(CODEC2_LOG_INFO,"mx size %d %d",mMXWidth,mMXHeight);
    return C2_OK;
}

c2_status_t C2Imagedec::onStop() {
    CODEC2_LOG(CODEC2_LOG_INFO, "%s", __func__);
    if (OK != resetDecoder()) {
        return C2_BAD_VALUE;
    }
    resetPlugin();
    return C2_OK;
}

void C2Imagedec::onReset() {
    CODEC2_LOG(CODEC2_LOG_INFO, "%s", __func__);
    onStop();
}

void C2Imagedec::onRelease() {
   CODEC2_LOG(CODEC2_LOG_INFO, "%s", __func__);
   deleteDecoder();
    if (mOutBlock) {
        mOutBlock.reset();
    }
}

c2_status_t C2Imagedec::onFlush_sm() {
    CODEC2_LOG(CODEC2_LOG_INFO, "%s", __func__);
    resetDecoder();
    resetPlugin();
    mSignaledOutputEos = false;
    mFirstPictureReviced = false;
    mPendingWorkFrameIndexes.clear();
    return C2_OK;
}

status_t C2Imagedec::initDecoder() {
    return OK;
}

status_t C2Imagedec::resetDecoder() {
    deleteDecoder();
    mDecInit = false;
    if (initDecoder() != OK) {
        return UNKNOWN_ERROR;
    }
    mSignaledError = false;
    return OK;
}

void C2Imagedec::resetPlugin() {
    mSignaledOutputEos = false;
    mTimeStart = mTimeEnd = systemTime();
}

status_t C2Imagedec::deleteDecoder() {
    return OK;
}

static void fillEmptyWork(const std::unique_ptr<C2Work> &work) {
    uint32_t flags = 0;
    if (work->input.flags & C2FrameData::FLAG_END_OF_STREAM) {
        flags |= C2FrameData::FLAG_END_OF_STREAM;
        CODEC2_LOG(CODEC2_LOG_INFO, "Signaling EOS");
    }
    work->worklets.front()->output.flags = (C2FrameData::flags_t)flags;
    work->worklets.front()->output.buffers.clear();
    work->worklets.front()->output.ordinal = work->input.ordinal;
    work->workletsProcessed = 1u;
}

void C2Imagedec::finishWork(uint64_t index, const std::unique_ptr<C2Work> &work) {

    std::shared_ptr<C2Buffer> buffer = createGraphicBuffer(std::move(mOutBlock),
                                                           C2Rect(mWidth, mHeight));
    mOutBlock = nullptr;
    {
        IntfImpl::Lock lock = mIntfImpl->lock();
        buffer->setInfo(mIntfImpl->getColorAspects_l());
    }

    class FillWork {
       public:
        FillWork(uint32_t flags, C2WorkOrdinalStruct ordinal,
                 const std::shared_ptr<C2Buffer>& buffer)
            : mFlags(flags), mOrdinal(ordinal), mBuffer(buffer) {}
        ~FillWork() = default;

        void operator()(const std::unique_ptr<C2Work>& work) {
            work->worklets.front()->output.flags = (C2FrameData::flags_t)mFlags;
            work->worklets.front()->output.buffers.clear();
            work->worklets.front()->output.ordinal = mOrdinal;
            work->workletsProcessed = 1u;
            work->result = C2_OK;
            ALOGE("push buffer ()");
            if (mBuffer) {
                ALOGE("push buffer mBuffer)");
                work->worklets.front()->output.buffers.push_back(mBuffer);
            }
            CODEC2_LOG(CODEC2_LOG_INFO, "Timestamp = %lld, index = %lld, w/%s buffer",
                  mOrdinal.timestamp.peekll(), mOrdinal.frameIndex.peekll(),
                  mBuffer ? "" : "o");
        }

       private:
        const uint32_t mFlags;
        const C2WorkOrdinalStruct mOrdinal;
        const std::shared_ptr<C2Buffer> mBuffer;
    };

    auto fillWork = [buffer](const std::unique_ptr<C2Work> &work) {
        ALOGE("push buffer");
        work->worklets.front()->output.flags = (C2FrameData::flags_t)0;
        work->worklets.front()->output.buffers.clear();
        work->worklets.front()->output.buffers.push_back(buffer);
        work->worklets.front()->output.ordinal = work->input.ordinal;
        work->workletsProcessed = 1u;
    };

    if (work && c2_cntr64_t(index) == work->input.ordinal.frameIndex) {
        bool eos = ((work->input.flags & C2FrameData::FLAG_END_OF_STREAM) != 0);
        // TODO: Check if cloneAndSend can be avoided by tracking number of frames remaining
        if (eos) {
            mOutIndex = index;
            C2WorkOrdinalStruct outOrdinal = work->input.ordinal;
            cloneAndSend(
                mOutIndex, work,
                FillWork(C2FrameData::FLAG_INCOMPLETE, outOrdinal, buffer));
            buffer.reset();
        } else {
            fillWork(work);
        }
    } else {
        if (work) {
            ALOGE("ok gone");
            finish(index, work->input.ordinal.customOrdinal.peeku(), fillWork);
        }else {
            ALOGE("ok none");
        }
    }
}

c2_status_t C2Imagedec::ensureDecoderState(const std::shared_ptr<C2BlockPool> &pool,uint32_t format, uint64_t platformUsage) {
    if (mOutBlock &&
            (mOutBlock->width() != ALIGN64(mWidth) || mOutBlock->height() != ALIGN2(mHeight))) {
        mOutBlock.reset();
    }
    CODEC2_LOG(CODEC2_LOG_DEBUG_LEVEL1, "Start fetchGraphicBlock,format %d, Required (%dx%d)", format,ALIGN64(mWidth), ALIGN2(mHeight));
    if (!mOutBlock) {
        //uint32_t format = HAL_PIXEL_FORMAT_YV12;
        C2MemoryUsage usage = { C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE };
        if (mDisPlayByVpp && platformUsage) {
            //format = HAL_PIXEL_FORMAT_YCRCB_420_SP;
            usage = { (C2MemoryUsage::CPU_READ | C2MemoryUsage::CPU_WRITE), platformUsage };
        }
        c2_status_t err =
            pool->fetchGraphicBlock(ALIGN64(mWidth), ALIGN2(mHeight), format, usage, &mOutBlock);
        if (err != C2_OK) {
            CODEC2_LOG(CODEC2_LOG_ERR, "FetchGraphicBlock for Output failed with status %d", err);
            return err;
        }
        CODEC2_LOG(CODEC2_LOG_DEBUG_LEVEL1, "FetchGraphicBlock done, Provided (%dx%d) Required (%dx%d)",
              mOutBlock->width(), mOutBlock->height(), ALIGN64(mWidth), ALIGN2(mHeight));
    }
    return C2_OK;
}

// TODO: can overall error checking be improved?
// TODO: allow configuration of color format and usage for graphic buffers instead
//       of hard coding them to HAL_PIXEL_FORMAT_YV12
// TODO: pass coloraspects information to surface
// TODO: test support for dynamic change in resolution
// TODO: verify if the decoder sent back all frames
void C2Imagedec::process(
        const std::unique_ptr<C2Work> &work,
        const std::shared_ptr<C2BlockPool> &pool,
        uint64_t platformUsage) {
    // Initialize output work
    work->result = C2_OK;
    work->workletsProcessed = 0u;
    work->worklets.front()->output.configUpdate.clear();
    work->worklets.front()->output.flags = work->input.flags;

    size_t inSize = 0u;
    uint32_t workIndex = work->input.ordinal.frameIndex.peeku() & 0xFFFFFFFF;
    C2ReadView rView = mDummyReadView;
    if (!work->input.buffers.empty()) {
        ALOGE("img: input is not empty %d-size %zu",workIndex,work->input.buffers.size());
        rView = work->input.buffers[0]->data().linearBlocks().front().map().get();
        inSize = rView.capacity();
        if (inSize && rView.error()) {
            CODEC2_LOG(CODEC2_LOG_ERR, "Read view map failed %d", rView.error());
            work->result = rView.error();
            return;
        }
    }
    uint8_t *inBuffer = const_cast<uint8_t *>(rView.data());
    bool codecConfig = ((work->input.flags & C2FrameData::FLAG_CODEC_CONFIG) !=0);
    bool eos = ((work->input.flags & C2FrameData::FLAG_END_OF_STREAM) != 0);

    bool frameHasData = (inSize > 0);
    bool flushPendingWork = (eos && !mPendingWorkFrameIndexes.empty());
    bool isHdr = false;
    // Config csd data
    if (codecConfig) {
        if (inSize > 0) {
            CODEC2_LOG(CODEC2_LOG_INFO, "Config num:%zu", inSize);
            if (mExtraData != NULL) {
                free(mExtraData);
            }
            mExtraData = (uint8_t *)malloc(inSize);
            if (mExtraData == NULL) {
                work->result = C2_NO_MEMORY;
                return;
            }
            memcpy(mExtraData, inBuffer, inSize);
        }
        CODEC2_LOG(CODEC2_LOG_INFO, "For %s don't input config pkt to ffmpeg", mDecoderName.c_str());
        fillEmptyWork(work);
        return;
    }else if (inSize > 2 && (inBuffer[inSize-2] != 0xff || inBuffer[inSize-1] != 0xd9)) {
        unsigned char ffd9[] = {0xff, 0xd9};
        if (mExtraData != NULL) {
            free(mExtraData);
            mExtraData = NULL;
        }
        mExtraData = (uint8_t *)malloc(inSize);
        if (mExtraData == NULL) {
            work->result = C2_NO_MEMORY;
            return;
        }
        memcpy(mExtraData, inBuffer, inSize);
        memcpy(mExtraData+inSize-2, ffd9, 2);
        inBuffer = mExtraData;
    }
    if (inSize > 2048) {
        std::string head = uint8ArrayToHexString(inBuffer,2048);
        if (head.find("6864722d6761696e2d6d6170") != string::npos && findEndStart(inBuffer,inSize)) {
            ALOGE("is hdr");
            isHdr = true;
        }
    }
    // Loop for resolution changed case.
    //while (frameHasData || flushPendingWork) {
    if (frameHasData || flushPendingWork) {
        ALOGE("img enter");
        decoder.UnloadFrame();
        int width = 0;
        int height = 0;
        libyuv::MJPGSize(inBuffer, inSize, &width, &height);
        if (width == 0 || height == 0) {
            ALOGE("img size error %dx%d", width, height);
            work->result = C2_BAD_VALUE ;
            return;
        }else {
            ALOGE("img enter %dx%d", width, height);
        }
        if (width > mMXWidth || height > mMXWidth) {
            mSignaledError = true;
            work->result = C2_BAD_VALUE;
            return;
        }
        ALOGE("img exit %dx%d", width, height);
        uint32_t format = isHdr? HAL_PIXEL_FORMAT_RGBA_1010102:HAL_PIXEL_FORMAT_YCRCB_420_SP;
        ALOGE("format %d",format);
        mWidth = width;
        mHeight = height;
        if (C2_OK != ensureDecoderState(pool, format, platformUsage)) {
            mSignaledError = true;
            work->result = C2_BAD_VALUE;
            return;
        }
        decoder.LoadFrame(inBuffer, inSize);

        C2GraphicView wView = mOutBlock->map().get();
        if (wView.error()) {
            CODEC2_LOG(CODEC2_LOG_ERR, "Graphic view map failed %d", wView.error());
            work->result = wView.error();
            return;
        }
        ALOGE("provided (%dx%d) required (%dx%d)",
           mOutBlock->width(), mOutBlock->height(), mWidth, mHeight);
        int ret = 0;
        if (isHdr) {
            decoder.UnloadFrame();
            uint8_t* mOutFile = const_cast<uint8_t*>(wView.data()[0]);
            UltraHdrAppInput appInput(inBuffer,inSize, uhdr_color_transfer_t::UHDR_CT_PQ, UHDR_IMG_FMT_32bppRGBA1010102,mDisplayRatio,false);
            ret = appInput.decode(mOutFile) >0?0:2;
        }else {
            int half_width = (ALIGN64(width) + 1) / 2;
            uint8_t* dstY = wView.data()[C2PlanarLayout::PLANE_Y];
            uint8_t* dstV = wView.data()[C2PlanarLayout::PLANE_V];

            if (decoder.GetColorSpace() == 3 && decoder.GetNumComponents() == 3 &&
                        decoder.GetVertSampFactor(0) == 2 && decoder.GetHorizSampFactor(0) == 1) {
                ret = JPEG2NV21(decoder, inBuffer, inSize, dstY, ALIGN64(width) , dstV,
                        half_width * 2, width, height, width, height);
                ALOGE("JPEG2NV21");
            }else {
                decoder.UnloadFrame();
                ret = libyuv::MJPGToNV21(inBuffer, inSize, dstY, ALIGN64(width) , dstV,
                            half_width * 2, width, height, width, height);
            }
        }
        ALOGE("after alloc ret %d",ret);
        if (mExtraData != NULL) {
            free(mExtraData);
            mExtraData = NULL;
        }
        if (ret != 0) {
            work->result = C2_BAD_VALUE;
            return;
        }

        // Set out pts
        work->input.ordinal.customOrdinal = 1;
        uint8_t *data = NULL;

        // For yuv dump
        if (mDumpYuvFp) {
         /* const uint8_t* const* data = wView.data();
            int size = mOutBlock->width() * mOutBlock->height() * 3 / 2;
            fwrite(data[0], size, 1, mDumpYuvFp); */
            int shift;
            for (int i = 0; i < 3; i++) {
                 shift = i>0 ? 1 : 0;
                 data = (uint8_t *)mPic->data[i];
                 for (int j = 0; j < mOutBlock->height()>>shift; j++) {
                      fwrite(data, sizeof(char), mOutBlock->width()>>shift, mDumpYuvFp);
                       data += mPic->linesize[i];
                 }
            }
        }

        if (!mPendingWorkFrameIndexes.empty()) {
            if (!flushPendingWork) {
                mPendingWorkFrameIndexes.push_back(work->input.ordinal.frameIndex.peeku());
            }
            finishWork(mPendingWorkFrameIndexes.front(), work);
            mPendingWorkFrameIndexes.pop_front();
        } else {
            ALOGE("finish work %d",workIndex);
            finishWork(workIndex, work);
        }
        ALOGE("mPendingWorkFrameIndexes.empty() %d",mPendingWorkFrameIndexes.empty());
        // Exit directly if no need flushPendingWork or flush done.
        if (!flushPendingWork || mPendingWorkFrameIndexes.empty()) {
            //break;
        }
    }
    if (eos) {
        fillEmptyWork(work);
    }
}
bool C2Imagedec::findEndStart(uint8_t* buffer,int size) {
    for (int i = 1024; i < (size - 1024); i++) {
        if (buffer[i] == 0xFF && buffer[i+1] == 0xD9 && buffer[i+2] == 0xFF && buffer[i+3] == 0xD8) {
            return true;
        }
    }
    return false;
}
c2_status_t C2Imagedec::drainInternal(
        uint32_t drainMode,
        const std::shared_ptr<C2BlockPool> &pool,
        const std::unique_ptr<C2Work> &work) {
    (void)pool;
    if (drainMode == NO_DRAIN) {
        CODEC2_LOG(CODEC2_LOG_ERR, "Drain with NO_DRAIN: no-op");
        return C2_OK;
    }
    if (drainMode == DRAIN_CHAIN) {
        CODEC2_LOG(CODEC2_LOG_ERR, "Drain with DRAIN_CHAIN not supported");
        return C2_OMITTED;
    }

    if (drainMode == DRAIN_COMPONENT_WITH_EOS &&
            work && work->workletsProcessed == 0u) {
        fillEmptyWork(work);
    }
    return C2_OK;
}


c2_status_t C2Imagedec::drain(
        uint32_t drainMode,
        const std::shared_ptr<C2BlockPool> &pool) {
    return drainInternal(drainMode, pool, nullptr);
}

class C2SoftImageFactory : public C2ComponentFactory {
public:
    C2SoftImageFactory(C2String decoderName)
          : mDecoderName(decoderName),
            mReflector(std::static_pointer_cast<C2ReflectorHelper>(
                    GetCodec2VendorComponentStore()->getParamReflector())){};

    c2_status_t createComponent(c2_node_id_t id, std::shared_ptr<C2Component>* const component,
                                ComponentDeleter deleter) override {
        UNUSED(deleter);
        *component = C2Imagedec::create(mDecoderName, id, std::make_shared<C2Imagedec::IntfImpl>(mDecoderName, mReflector));
        return *component ? C2_OK : C2_NO_MEMORY;
    }
    c2_status_t createInterface(c2_node_id_t id,
                                std::shared_ptr<C2ComponentInterface>* const interface,
                                InterfaceDeleter deleter) override {
        UNUSED(deleter);
        *interface =
                std::shared_ptr<C2ComponentInterface>(new SimpleInterface<C2Imagedec::IntfImpl>(
                        mDecoderName.c_str(), id,
                        std::make_shared<C2Imagedec::IntfImpl>(mDecoderName, mReflector)));
        return C2_OK;
    }
    ~C2SoftImageFactory() override = default;

private:
    const C2String mDecoderName;
    std::shared_ptr<C2ReflectorHelper> mReflector;
};

}  // namespace android


#define CreateC2SoftImageFactory(type) \
        extern "C" ::C2ComponentFactory* CreateC2SoftImage##type##Factory(bool secureMode) {\
             ALOGE("%s", __func__);\
             UNUSED(secureMode);\
             return new ::android::C2SoftImageFactory(android::k##type##DecoderName);\
        }

#define DestroyC2SoftImageFactory(type) \
    extern "C" void DestroyC2SoftImage##type##Factory(::C2ComponentFactory* factory) {\
        ALOGE("%s", __func__);\
        delete factory;\
    }

CreateC2SoftImageFactory(JPEG)

DestroyC2SoftImageFactory(JPEG)

