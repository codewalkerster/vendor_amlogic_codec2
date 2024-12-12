/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_TAG "AmMediaProxyConsumer"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <utils/Log.h>

#include "AmlMediaProxyConsumer.h"
#include "AmlVideoUserdata.h"
#include "AmlMediaErrorCodes.h"

namespace android {

MediaProxyConsumer::MediaProxyConsumer(void (*callback)(void*, const struct aml_video_user_data&), void* privateData, uint32_t messageTypes)
        :mMediaProxyConsumerLibHandle(nullptr), mCallback(callback), mPrivateData(privateData),
        mMessageTypes(messageTypes), mRunning(false) {
    mMediaProxyConsumerLibHandle = dlopen("libmediaproxy_consumer.so", RTLD_NOW);
    if (mMediaProxyConsumerLibHandle == nullptr) {
        ALOGE("Failed to load libmediaproxy_consumer.so");
        return;
    }
    MediaProxyConsumer_init = (DMediaProxyConsumer_init)
            dlsym(mMediaProxyConsumerLibHandle, "initialize");
    MediaProxyConsumer_registerMsgType = (DMediaProxyConsumer_registerMsgType)
            dlsym(mMediaProxyConsumerLibHandle, "registerMsgType");
    MediaProxyConsumer_readData = (DMediaProxyConsumer_readData)
            dlsym(mMediaProxyConsumerLibHandle, "readData");
    MediaProxyConsumer_destroy = (DMediaProxyConsumer_destroy)
            dlsym(mMediaProxyConsumerLibHandle, "destroy");
    if (MediaProxyConsumer_init == nullptr || MediaProxyConsumer_registerMsgType == nullptr ||
        MediaProxyConsumer_readData == nullptr || MediaProxyConsumer_destroy == nullptr) {
        ALOGE("Failed to load symbols from libmediaproxyconsumer.so");
        dlclose(mMediaProxyConsumerLibHandle);
        mMediaProxyConsumerLibHandle = nullptr;
        return;
    }

    mProxyHandle = MediaProxyConsumer_init();
    if (mProxyHandle < 0) {
        ALOGE("Failed to initialize media proxy consumer");
        return;
    }
    if (MediaProxyConsumer_registerMsgType(mProxyHandle, mMessageTypes)) {
        ALOGW("Failed to register message type %08x", mMessageTypes);
    }
    this->mRunning = true;
    pthread_create(&mThread, NULL, consumerThread, this);
}

MediaProxyConsumer::~MediaProxyConsumer() {
    mRunning = false;
    pthread_join(mThread, NULL);
    if (mProxyHandle) {
        MediaProxyConsumer_destroy(mProxyHandle);
    }
    if (mMediaProxyConsumerLibHandle)
        dlclose(mMediaProxyConsumerLibHandle);
}

void* MediaProxyConsumer::consumerThread(void *arg) {
    MediaProxyConsumer* consumer = (MediaProxyConsumer*) arg;
    struct aml_video_user_data userData;
    while (consumer->mRunning) {
        memset(&userData, 0, sizeof(userData));
        if (0 > consumer->MediaProxyConsumer_readData(consumer->mProxyHandle, &userData)) {
            if (errno != ETIMEDOUT) {
                ALOGE("Failed to read data from media proxy, errno=%s", strerror(errno));
            } else {
                ALOGV("Timeout reading data from media proxy");
            }
            // wait for a while and try again
            ::usleep(100000);
            continue;
        }
       if (consumer->mRunning && consumer->mCallback != nullptr) {
            consumer->mCallback(consumer->mPrivateData, (const struct aml_video_user_data&) userData);
        }
    }
    consumer->mRunning = false;
    return NULL;
}
}