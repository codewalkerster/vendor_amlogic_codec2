/*
 **
 ** Copyright 2019 The Android Open Source Project
 **
 ** Licensed under the Apache License, Version 2.0 (the "License");
 ** you may not use this file except in compliance with the License.
 ** You may obtain a copy of the License at
 **
 **     http://www.apache.org/licenses/LICENSE-2.0
 **
 ** Unless required by applicable law or agreed to in writing, software
 ** distributed under the License is distributed on an "AS IS" BASIS,
 ** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 ** See the License for the specific language governing permissions and
 ** limitations under the License.
 */
#ifndef OMX_AML_MEDIAPROXY_CONSUMER_H_
#define OMX_AML_MEDIAPROXY_CONSUMER_H_

#include <stdint.h>
#include <pthread.h>

#include "AmlVideoUserdata.h"

#define AM_MEDIA_PROXY_CONSUMER_MAX_MESSAGE_TYPE        6

namespace android {

typedef int (*DMediaProxyConsumer_init)();
typedef int (*DMediaProxyConsumer_registerMsgType)(int handle, int type);
typedef int (*DMediaProxyConsumer_readData)(int handle, struct aml_video_user_data *userData);
typedef void (*DMediaProxyConsumer_destroy)(int handle);

class MediaProxyConsumer {

public:
    MediaProxyConsumer(void (*callback)(void*, const struct aml_video_user_data&) = nullptr, void* privateData = nullptr, uint32_t messageTypes = 0);
    ~MediaProxyConsumer();
    bool LibHandlValid() {return !!mMediaProxyConsumerLibHandle;}
    DMediaProxyConsumer_init              MediaProxyConsumer_init;
    DMediaProxyConsumer_registerMsgType   MediaProxyConsumer_registerMsgType;
    DMediaProxyConsumer_readData          MediaProxyConsumer_readData;
    DMediaProxyConsumer_destroy           MediaProxyConsumer_destroy;

    static void* consumerThread(void *arg);
private:
    void* mMediaProxyConsumerLibHandle;
    void (*mCallback)(void*, const struct aml_video_user_data&);
    void* mPrivateData;
    uint32_t mMessageTypes;
    pthread_t mThread;
    bool mRunning;
    int mProxyHandle;
};
}
#endif //OMX_AML_MEDIAPROXY_CONSUMER_WRAPPER_H_
