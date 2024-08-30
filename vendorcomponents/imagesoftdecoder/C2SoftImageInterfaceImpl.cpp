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

#include <media/stagefright/MediaDefs.h>
#include <C2PlatformSupport.h>
#include <android/hardware_buffer.h>
#include <C2VendorSupport.h>
#include <C2VendorProperty.h>
#include <C2VendorDebug.h>
#include <C2SoftImageInterfaceImpl.h>
#include <AmMediaDefsExt.h>


namespace android {

// Use basic graphic block pool/allocator as default.
const C2BlockPool::local_id_t kDefaultOutputBlockPool = C2PlatformAllocatorStore::GRALLOC;//C2PlatformAllocatorStore::BUFFERQUEUE;

constexpr uint32_t kDefaultOutputDelay = 8;             // The output buffer margin during initialization.
constexpr uint32_t kMaxOutputDelay = 32;                // Max output delay.
constexpr uint32_t kDefaultInputDelay = 4;              // The input buffer margin during initialization.
constexpr uint32_t kMaxInputDelay = 4;                  // Max input delay.


// static
C2R C2Imagedec::IntfImpl::ProfileLevelSetter(bool mayBlock,
                                                 C2P<C2StreamProfileLevelInfo::input>& info) {
    (void)mayBlock;
    (void)info;
    return C2R::Ok();
}

// static
C2R C2Imagedec::IntfImpl::SizeSetter(bool mayBlock, const C2P<C2StreamPictureSizeInfo::output> &oldMe,
                      C2P<C2StreamPictureSizeInfo::output> &me) {
    (void)mayBlock;
    C2R res = C2R::Ok();
    /*if (!me.F(me.v.width).supportsAtAll(me.v.width)) {
        //coverity[Memory - illegal accesses]
        res = res.plus(C2SettingResultBuilder::BadValue(me.F(me.v.width)));
        me.set().width = oldMe.v.width;
    }
    if (!me.F(me.v.height).supportsAtAll(me.v.height)) {
        //coverity[Memory - illegal accesses]
        res = res.plus(C2SettingResultBuilder::BadValue(me.F(me.v.height)));
        me.set().height = oldMe.v.height;
    }*/
    return res;
}

// static
C2R C2Imagedec::IntfImpl::MaxPictureSizeSetter(bool mayBlock, C2P<C2StreamMaxPictureSizeTuning::output> &me,
                                    const C2P<C2StreamPictureSizeInfo::output> &size) {
    (void)mayBlock;
    return C2R::Ok();
}

// static
C2R C2Imagedec::IntfImpl::DefaultColorAspectsSetter(bool mayBlock, C2P<C2StreamColorAspectsTuning::output> &me) {
    (void)mayBlock;
    if (me.v.range > C2Color::RANGE_OTHER) {
            me.set().range = C2Color::RANGE_OTHER;
    }
    if (me.v.primaries > C2Color::PRIMARIES_OTHER) {
            me.set().primaries = C2Color::PRIMARIES_OTHER;
    }
    if (me.v.transfer > C2Color::TRANSFER_OTHER) {
            me.set().transfer = C2Color::TRANSFER_OTHER;
    }
    if (me.v.matrix > C2Color::MATRIX_OTHER) {
            me.set().matrix = C2Color::MATRIX_OTHER;
    }
    return C2R::Ok();
}

// static
C2R C2Imagedec::IntfImpl::CodedColorAspectsSetter(bool mayBlock, C2P<C2StreamColorAspectsInfo::input> &me) {
    (void)mayBlock;
    if (me.v.range > C2Color::RANGE_OTHER) {
            me.set().range = C2Color::RANGE_OTHER;
    }
    if (me.v.primaries > C2Color::PRIMARIES_OTHER) {
            me.set().primaries = C2Color::PRIMARIES_OTHER;
    }
    if (me.v.transfer > C2Color::TRANSFER_OTHER) {
            me.set().transfer = C2Color::TRANSFER_OTHER;
    }
    if (me.v.matrix > C2Color::MATRIX_OTHER) {
            me.set().matrix = C2Color::MATRIX_OTHER;
    }
    return C2R::Ok();
}

// static
C2R C2Imagedec::IntfImpl::ColorAspectsSetter(bool mayBlock, C2P<C2StreamColorAspectsInfo::output> &me,
                          const C2P<C2StreamColorAspectsTuning::output> &def,
                          const C2P<C2StreamColorAspectsInfo::input> &coded) {
    (void)mayBlock;
    // take default values for all unspecified fields, and coded values for specified ones
    me.set().range = coded.v.range == RANGE_UNSPECIFIED ? def.v.range : coded.v.range;
    me.set().primaries = coded.v.primaries == PRIMARIES_UNSPECIFIED
            ? def.v.primaries : coded.v.primaries;
    me.set().transfer = coded.v.transfer == TRANSFER_UNSPECIFIED
            ? def.v.transfer : coded.v.transfer;
    me.set().matrix = coded.v.matrix == MATRIX_UNSPECIFIED ? def.v.matrix : coded.v.matrix;
    return C2R::Ok();
}

const char* C2Imagedec::IntfImpl::ConvertComponentNameToMimeType(const char* componentName) {
    if (componentName == NULL) {
        CODEC2_LOG(CODEC2_LOG_ERR, "%s componentName is NULL!", __func__);
        return "NA";
    }
    CODEC2_LOG(CODEC2_LOG_INFO,"codecname:%s", componentName);
    return const_cast<char *>(MEDIA_MIMETYPE_IMAGE_JPEG);
}

c2_status_t C2Imagedec::IntfImpl::config(
    const std::vector<C2Param*> &params, c2_blocking_t mayBlock,
    std::vector<std::unique_ptr<C2SettingResult>>* const failures,
    bool updateParams,
    std::vector<std::shared_ptr<C2Param>> *changes) {
    C2InterfaceHelper::config(params, mayBlock, failures, updateParams, changes);

    return C2_OK;
}

C2Imagedec::IntfImpl::IntfImpl(C2String name, const std::shared_ptr<C2ReflectorHelper>& helper)
      : C2InterfaceHelper(helper) {
    setDerivedInstance(this);

    const char* mime = ConvertComponentNameToMimeType(name.c_str());

    // profile and level

    // base setting
    onBaseDeclareParam();

    // media type
    onMediaTypeDeclareParam(mime);

    // input delay
    onInputDelayDeclareParam();

    // output delay
    onOutputDelayDeclareParam();

    // buffer size
    onBufferSizeDeclareParam();

    // buffer pool
    onBufferPoolDeclareParam();

    // ColorAspects
    onColorAspectsDeclareParam(helper);
}

void C2Imagedec::IntfImpl::onBaseDeclareParam() {
    addParameter(
            DefineParam(mApiFeatures, C2_PARAMKEY_API_FEATURES)
            .withConstValue(new C2ApiFeaturesSetting(C2Config::api_feature_t(
                    API_REFLECTION |
                    API_VALUES |
                    API_CURRENT_VALUES |
                    API_DEPENDENCY |
                    API_SAME_INPUT_BUFFER)))
            .build());
    addParameter(
             DefineParam(mDomain, C2_PARAMKEY_COMPONENT_DOMAIN)
             .withConstValue(new C2ComponentDomainSetting(C2Component::DOMAIN_IMAGE))
             .build());

    addParameter(
            DefineParam(mKind, C2_PARAMKEY_COMPONENT_KIND)
            .withConstValue(
                    new C2ComponentKindSetting(C2Component::KIND_DECODER))
            .build());
}

void C2Imagedec::IntfImpl::onMediaTypeDeclareParam(const char* mime) {
    addParameter(
            DefineParam(mInputFormat, C2_PARAMKEY_INPUT_STREAM_BUFFER_TYPE)
            .withConstValue(new C2StreamBufferTypeSetting::input(0u, C2BufferData::LINEAR))
            .build());

    addParameter(DefineParam(mOutputFormat, C2_PARAMKEY_OUTPUT_STREAM_BUFFER_TYPE)
            .withConstValue(new C2StreamBufferTypeSetting::output(0u, C2BufferData::GRAPHIC))
            .build());

    addParameter(DefineParam(mInputMediaType, C2_PARAMKEY_INPUT_MEDIA_TYPE)
            .withConstValue(AllocSharedString<C2PortMediaTypeSetting::input>(mime))
            .build());

    addParameter(DefineParam(mOutputMediaType, C2_PARAMKEY_OUTPUT_MEDIA_TYPE)
            .withConstValue(AllocSharedString<C2PortMediaTypeSetting::output>(MEDIA_MIMETYPE_VIDEO_RAW))
            .build());
}

void C2Imagedec::IntfImpl::onInputDelayDeclareParam() {
    addParameter(
            DefineParam(mActualInputDelay, C2_PARAMKEY_INPUT_DELAY)
            .withDefault(new C2PortActualDelayTuning::input(kDefaultInputDelay))
            .withFields({C2F(mActualInputDelay, value).inRange(0, kMaxInputDelay)})
            .withSetter(Setter<decltype(*mActualInputDelay)>::StrictValueWithNoDeps)
            .build());
}

void C2Imagedec::IntfImpl::onOutputDelayDeclareParam() {
    addParameter(
            DefineParam(mActualOutputDelay, C2_PARAMKEY_OUTPUT_DELAY)
            .withDefault(new C2PortActualDelayTuning::output(kDefaultOutputDelay))
            .withFields({C2F(mActualOutputDelay, value).inRange(0, kMaxOutputDelay)})
            .withSetter(Setter<decltype(*mActualOutputDelay)>::StrictValueWithNoDeps)
            .build());
}

void C2Imagedec::IntfImpl::onBufferSizeDeclareParam() {
    // coded and output picture size is the same for this codec
    addParameter(
            DefineParam(mSize, C2_PARAMKEY_PICTURE_SIZE)
            .withDefault(new C2StreamPictureSizeInfo::output(0u, 1920, 1920))
            .withFields({
                C2F(mSize, width).inRange(2, 8160, 2),
                C2F(mSize, height).inRange(2, 8160, 2),
            })
            .withSetter(SizeSetter)
            .build());

    addParameter(
            DefineParam(mMaxSize, C2_PARAMKEY_MAX_PICTURE_SIZE)
            .withDefault(new C2StreamMaxPictureSizeTuning::output(0u, 1920, 1920))
            .withFields({
                C2F(mSize, width).inRange(2, 8160, 2),
                C2F(mSize, height).inRange(2, 8160, 2),
            })
            .withSetter(MaxPictureSizeSetter, mSize)
            .build());

    // App may set a smaller value for maximum of input buffer size than actually required
    // by mistake. C2SoftVdec overrides it if the value specified by app is smaller than
    // the calculated value in MaxSizeCalculator().
    // This value is the default maximum of linear buffer size (kLinearBufferSize) in
    // CCodecBufferChannel.cpp.
    constexpr static size_t kLinearBufferSize = 6 * 1024 * 1024;
    struct LocalCalculator {
        static C2R MaxSizeCalculator(bool mayBlock, C2P<C2StreamMaxBufferSizeInfo::input>& me,
                                        const C2P<C2StreamMaxPictureSizeTuning::output>& size) {
            (void)mayBlock;
            ALOGD("MaxSizeCalculator enter");

            //app may set too small
            me.set().value = kLinearBufferSize;
            return C2R::Ok();
        }
    };

    addParameter(
            DefineParam(mMaxInputSize, C2_PARAMKEY_INPUT_MAX_BUFFER_SIZE)
            .withDefault(new C2StreamMaxBufferSizeInfo::input(0u, kLinearBufferSize))
            .withFields({
                C2F(mMaxInputSize, value).any(),
            })
            .calculatedAs(LocalCalculator::MaxSizeCalculator, mMaxSize)
            .build());
}

void C2Imagedec::IntfImpl::onBufferPoolDeclareParam() {
    C2Allocator::id_t inputAllocators[] = { C2PlatformAllocatorStore::DMABUFHEAP };

    C2Allocator::id_t outputAllocators[] = {C2PlatformAllocatorStore::GRALLOC};

    addParameter(
            DefineParam(mInputAllocatorIds, C2_PARAMKEY_INPUT_ALLOCATORS)
            .withConstValue(C2PortAllocatorsTuning::input::AllocShared(inputAllocators))
            .build());

    addParameter(
            DefineParam(mOutputAllocatorIds, C2_PARAMKEY_OUTPUT_ALLOCATORS)
            .withConstValue(C2PortAllocatorsTuning::output::AllocShared(outputAllocators))
            .build());

    C2BlockPool::local_id_t outputBlockPools[] = {kDefaultOutputBlockPool};

    addParameter(
            DefineParam(mOutputBlockPoolIds, C2_PARAMKEY_OUTPUT_BLOCK_POOLS)
            .withDefault(C2PortBlockPoolsTuning::output::AllocShared(outputBlockPools))
            .withFields({C2F(mOutputBlockPoolIds, m.values[0]).any(),
                            C2F(mOutputBlockPoolIds, m.values).inRange(0, 1)})
            .withSetter(Setter<C2PortBlockPoolsTuning::output>::NonStrictValuesWithNoDeps)
            .build());
}

void C2Imagedec::IntfImpl::onColorAspectsDeclareParam(const std::shared_ptr<C2ReflectorHelper> &helper) {
    C2ChromaOffsetStruct locations[1] = { C2ChromaOffsetStruct::ITU_YUV_420_0() };
    std::shared_ptr<C2StreamColorInfo::output> defaultColorInfo =
        C2StreamColorInfo::output::AllocShared(
                1u, 0u, 8u /* bitDepth */, C2Color::YUV_420);
    memcpy(defaultColorInfo->m.locations, locations, sizeof(locations));

    defaultColorInfo =
        C2StreamColorInfo::output::AllocShared(
                { C2ChromaOffsetStruct::ITU_YUV_420_0() },
                0u, 8u /* bitDepth */, C2Color::YUV_420);
    helper->addStructDescriptors<C2ChromaOffsetStruct>();

    addParameter(
            DefineParam(mColorInfo, C2_PARAMKEY_CODED_COLOR_INFO)
            .withConstValue(defaultColorInfo)
            .build());

    addParameter(
            DefineParam(mDefaultColorAspects, C2_PARAMKEY_DEFAULT_COLOR_ASPECTS)
            .withDefault(new C2StreamColorAspectsTuning::output(
                    0u, C2Color::RANGE_UNSPECIFIED, C2Color::PRIMARIES_UNSPECIFIED,
                    C2Color::TRANSFER_UNSPECIFIED, C2Color::MATRIX_UNSPECIFIED))
            .withFields({
                C2F(mDefaultColorAspects, range).inRange(
                            C2Color::RANGE_UNSPECIFIED,     C2Color::RANGE_OTHER),
                C2F(mDefaultColorAspects, primaries).inRange(
                            C2Color::PRIMARIES_UNSPECIFIED, C2Color::PRIMARIES_OTHER),
                C2F(mDefaultColorAspects, transfer).inRange(
                            C2Color::TRANSFER_UNSPECIFIED,  C2Color::TRANSFER_OTHER),
                C2F(mDefaultColorAspects, matrix).inRange(
                            C2Color::MATRIX_UNSPECIFIED,    C2Color::MATRIX_OTHER)
            })
            .withSetter(DefaultColorAspectsSetter)
            .build());

    addParameter(
            DefineParam(mCodedColorAspects, C2_PARAMKEY_VUI_COLOR_ASPECTS)
            .withDefault(new C2StreamColorAspectsInfo::input(
                    0u, C2Color::RANGE_UNSPECIFIED, C2Color::PRIMARIES_UNSPECIFIED,
                    C2Color::TRANSFER_UNSPECIFIED, C2Color::MATRIX_UNSPECIFIED))
            .withFields({
                C2F(mCodedColorAspects, range).inRange(
                            C2Color::RANGE_UNSPECIFIED,     C2Color::RANGE_OTHER),
                C2F(mCodedColorAspects, primaries).inRange(
                            C2Color::PRIMARIES_UNSPECIFIED, C2Color::PRIMARIES_OTHER),
                C2F(mCodedColorAspects, transfer).inRange(
                            C2Color::TRANSFER_UNSPECIFIED,  C2Color::TRANSFER_OTHER),
                C2F(mCodedColorAspects, matrix).inRange(
                            C2Color::MATRIX_UNSPECIFIED,    C2Color::MATRIX_OTHER)
            })
            .withSetter(CodedColorAspectsSetter)
            .build());

    addParameter(
            DefineParam(mColorAspects, C2_PARAMKEY_COLOR_ASPECTS)
            .withDefault(new C2StreamColorAspectsInfo::output(
                    0u, C2Color::RANGE_UNSPECIFIED, C2Color::PRIMARIES_UNSPECIFIED,
                    C2Color::TRANSFER_UNSPECIFIED, C2Color::MATRIX_UNSPECIFIED))
            .withFields({
                C2F(mColorAspects, range).inRange(
                            C2Color::RANGE_UNSPECIFIED,     C2Color::RANGE_OTHER),
                C2F(mColorAspects, primaries).inRange(
                            C2Color::PRIMARIES_UNSPECIFIED, C2Color::PRIMARIES_OTHER),
                C2F(mColorAspects, transfer).inRange(
                            C2Color::TRANSFER_UNSPECIFIED,  C2Color::TRANSFER_OTHER),
                C2F(mColorAspects, matrix).inRange(
                            C2Color::MATRIX_UNSPECIFIED,    C2Color::MATRIX_OTHER)
            })
            .withSetter(ColorAspectsSetter, mDefaultColorAspects, mCodedColorAspects)
            .build());
    addParameter(
            DefineParam(mPixelFormat, C2_PARAMKEY_PIXEL_FORMAT)
            .withDefault(new C2StreamPixelFormatInfo::output(
                              0u, HAL_PIXEL_FORMAT_YCBCR_420_888))
            .withFields({C2F(mPixelFormat, value).any()})
            .withSetter((Setter<decltype(*mPixelFormat)>::StrictValueWithNoDeps))
            .build());
}

}// namespace android

