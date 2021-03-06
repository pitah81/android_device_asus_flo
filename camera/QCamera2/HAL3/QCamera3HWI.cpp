/* Copyright (c) 2012-2015, The Linux Foundataion. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*/

#define LOG_TAG "QCamera3HWI"

#include <cutils/properties.h>
#include <hardware/camera3.h>
#include <camera/CameraMetadata.h>
#include <stdlib.h>
#include <utils/Log.h>
#include <utils/Errors.h>
#include <ui/Fence.h>
#include <gralloc_priv.h>
#include "QCamera3HWI.h"
#include "QCamera3Mem.h"
#include "QCamera3Channel.h"
#include "QCamera3PostProc.h"

using namespace android;

namespace qcamera {
#define DATA_PTR(MEM_OBJ,INDEX) MEM_OBJ->getPtr( INDEX )

#define EMPTY_PIPELINE_DELAY 2

cam_capability_t *gCamCapability[MM_CAMERA_MAX_NUM_SENSORS];
parm_buffer_t *prevSettings;
const camera_metadata_t *gStaticMetadata[MM_CAMERA_MAX_NUM_SENSORS];

pthread_mutex_t QCamera3HardwareInterface::mCameraSessionLock =
    PTHREAD_MUTEX_INITIALIZER;
unsigned int QCamera3HardwareInterface::mCameraSessionActive = 0;

const QCamera3HardwareInterface::QCameraMap QCamera3HardwareInterface::EFFECT_MODES_MAP[] = {
    { ANDROID_CONTROL_EFFECT_MODE_OFF,       CAM_EFFECT_MODE_OFF },
    { ANDROID_CONTROL_EFFECT_MODE_MONO,       CAM_EFFECT_MODE_MONO },
    { ANDROID_CONTROL_EFFECT_MODE_NEGATIVE,   CAM_EFFECT_MODE_NEGATIVE },
    { ANDROID_CONTROL_EFFECT_MODE_SOLARIZE,   CAM_EFFECT_MODE_SOLARIZE },
    { ANDROID_CONTROL_EFFECT_MODE_SEPIA,      CAM_EFFECT_MODE_SEPIA },
    { ANDROID_CONTROL_EFFECT_MODE_POSTERIZE,  CAM_EFFECT_MODE_POSTERIZE },
    { ANDROID_CONTROL_EFFECT_MODE_WHITEBOARD, CAM_EFFECT_MODE_WHITEBOARD },
    { ANDROID_CONTROL_EFFECT_MODE_BLACKBOARD, CAM_EFFECT_MODE_BLACKBOARD },
    { ANDROID_CONTROL_EFFECT_MODE_AQUA,       CAM_EFFECT_MODE_AQUA }
};

const QCamera3HardwareInterface::QCameraMap QCamera3HardwareInterface::WHITE_BALANCE_MODES_MAP[] = {
    { ANDROID_CONTROL_AWB_MODE_OFF,             CAM_WB_MODE_OFF },
    { ANDROID_CONTROL_AWB_MODE_AUTO,            CAM_WB_MODE_AUTO },
    { ANDROID_CONTROL_AWB_MODE_INCANDESCENT,    CAM_WB_MODE_INCANDESCENT },
    { ANDROID_CONTROL_AWB_MODE_FLUORESCENT,     CAM_WB_MODE_FLUORESCENT },
    { ANDROID_CONTROL_AWB_MODE_WARM_FLUORESCENT,CAM_WB_MODE_WARM_FLUORESCENT},
    { ANDROID_CONTROL_AWB_MODE_DAYLIGHT,        CAM_WB_MODE_DAYLIGHT },
    { ANDROID_CONTROL_AWB_MODE_CLOUDY_DAYLIGHT, CAM_WB_MODE_CLOUDY_DAYLIGHT },
    { ANDROID_CONTROL_AWB_MODE_TWILIGHT,        CAM_WB_MODE_TWILIGHT },
    { ANDROID_CONTROL_AWB_MODE_SHADE,           CAM_WB_MODE_SHADE }
};

const QCamera3HardwareInterface::QCameraMap QCamera3HardwareInterface::SCENE_MODES_MAP[] = {
    { ANDROID_CONTROL_SCENE_MODE_FACE_PRIORITY,  CAM_SCENE_MODE_OFF },
    { ANDROID_CONTROL_SCENE_MODE_ACTION,         CAM_SCENE_MODE_ACTION },
    { ANDROID_CONTROL_SCENE_MODE_PORTRAIT,       CAM_SCENE_MODE_PORTRAIT },
    { ANDROID_CONTROL_SCENE_MODE_LANDSCAPE,      CAM_SCENE_MODE_LANDSCAPE },
    { ANDROID_CONTROL_SCENE_MODE_NIGHT,          CAM_SCENE_MODE_NIGHT },
    { ANDROID_CONTROL_SCENE_MODE_NIGHT_PORTRAIT, CAM_SCENE_MODE_NIGHT_PORTRAIT },
    { ANDROID_CONTROL_SCENE_MODE_THEATRE,        CAM_SCENE_MODE_THEATRE },
    { ANDROID_CONTROL_SCENE_MODE_BEACH,          CAM_SCENE_MODE_BEACH },
    { ANDROID_CONTROL_SCENE_MODE_SNOW,           CAM_SCENE_MODE_SNOW },
    { ANDROID_CONTROL_SCENE_MODE_SUNSET,         CAM_SCENE_MODE_SUNSET },
    { ANDROID_CONTROL_SCENE_MODE_STEADYPHOTO,    CAM_SCENE_MODE_ANTISHAKE },
    { ANDROID_CONTROL_SCENE_MODE_FIREWORKS ,     CAM_SCENE_MODE_FIREWORKS },
    { ANDROID_CONTROL_SCENE_MODE_SPORTS ,        CAM_SCENE_MODE_SPORTS },
    { ANDROID_CONTROL_SCENE_MODE_PARTY,          CAM_SCENE_MODE_PARTY },
    { ANDROID_CONTROL_SCENE_MODE_CANDLELIGHT,    CAM_SCENE_MODE_CANDLELIGHT },
    { ANDROID_CONTROL_SCENE_MODE_BARCODE,        CAM_SCENE_MODE_BARCODE}
};

const QCamera3HardwareInterface::QCameraMap QCamera3HardwareInterface::FOCUS_MODES_MAP[] = {
    { ANDROID_CONTROL_AF_MODE_OFF,                CAM_FOCUS_MODE_FIXED },
    { ANDROID_CONTROL_AF_MODE_AUTO,               CAM_FOCUS_MODE_AUTO },
    { ANDROID_CONTROL_AF_MODE_MACRO,              CAM_FOCUS_MODE_MACRO },
    { ANDROID_CONTROL_AF_MODE_EDOF,               CAM_FOCUS_MODE_EDOF },
    { ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE, CAM_FOCUS_MODE_CONTINOUS_PICTURE },
    { ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO,   CAM_FOCUS_MODE_CONTINOUS_VIDEO }
};

const QCamera3HardwareInterface::QCameraMap QCamera3HardwareInterface::ANTIBANDING_MODES_MAP[] = {
    { ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF,  CAM_ANTIBANDING_MODE_OFF },
    { ANDROID_CONTROL_AE_ANTIBANDING_MODE_50HZ, CAM_ANTIBANDING_MODE_50HZ },
    { ANDROID_CONTROL_AE_ANTIBANDING_MODE_60HZ, CAM_ANTIBANDING_MODE_60HZ },
    { ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO, CAM_ANTIBANDING_MODE_AUTO }
};

const QCamera3HardwareInterface::QCameraMap QCamera3HardwareInterface::AE_FLASH_MODE_MAP[] = {
    { ANDROID_CONTROL_AE_MODE_OFF,                  CAM_FLASH_MODE_OFF },
    { ANDROID_CONTROL_AE_MODE_ON,                   CAM_FLASH_MODE_OFF },
    { ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH,        CAM_FLASH_MODE_AUTO},
    { ANDROID_CONTROL_AE_MODE_ON_ALWAYS_FLASH,      CAM_FLASH_MODE_ON  },
    { ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH_REDEYE, CAM_FLASH_MODE_AUTO}
};

const QCamera3HardwareInterface::QCameraMap QCamera3HardwareInterface::FLASH_MODES_MAP[] = {
    { ANDROID_FLASH_MODE_OFF,    CAM_FLASH_MODE_OFF  },
    { ANDROID_FLASH_MODE_SINGLE, CAM_FLASH_MODE_SINGLE },
    { ANDROID_FLASH_MODE_TORCH,  CAM_FLASH_MODE_TORCH }
};

const int32_t available_thumbnail_sizes[] = {0, 0,
                                             176, 144,
                                             320, 240,
                                             432, 288,
                                             480, 288,
                                             512, 288,
                                             512, 384};

camera3_device_ops_t QCamera3HardwareInterface::mCameraOps = {
    .initialize =                         QCamera3HardwareInterface::initialize,
    .configure_streams =                  QCamera3HardwareInterface::configure_streams,
    .register_stream_buffers =            NULL,
    .construct_default_request_settings = QCamera3HardwareInterface::construct_default_request_settings,
    .process_capture_request =            QCamera3HardwareInterface::process_capture_request,
    .get_metadata_vendor_tag_ops =        QCamera3HardwareInterface::get_metadata_vendor_tag_ops,
    .dump =                               QCamera3HardwareInterface::dump,
    .flush =                              QCamera3HardwareInterface::flush,
    .reserved =                           {0},
};

int QCamera3HardwareInterface::kMaxInFlight = 5;

/*===========================================================================
 * FUNCTION   : QCamera3HardwareInterface
 *
 * DESCRIPTION: constructor of QCamera3HardwareInterface
 *
 * PARAMETERS :
 *   @cameraId  : camera ID
 *
 * RETURN     : none
 *==========================================================================*/
QCamera3HardwareInterface::QCamera3HardwareInterface(int cameraId)
    : mCameraId(cameraId),
      mCameraHandle(NULL),
      mCameraOpened(false),
      mCameraInitialized(false),
      mCallbackOps(NULL),
      mInputStream(NULL),
      mMetadataChannel(NULL),
      mPictureChannel(NULL),
      mFirstRequest(false),
      mParamHeap(NULL),
      mParameters(NULL),
      mJpegSettings(NULL),
      mIsZslMode(false),
      m_pPowerModule(NULL),
      mPrecaptureId(0),
      mAeMode(0),
      mAeLock(0),
      mAfMode(0),
      mAfTrigger(),
      mAwbLock(0),
      mAwbMode(0),
      mColorCorrectMode(0),
      mColorCorrectGains({{1.0}}),
      mEdgeMode(0),
      mSensorFrameDuration(0),
      mEffectMode(0),
      mSceneMode(0),
      mTonemapMode(0)
{
    mCameraDevice.common.tag = HARDWARE_DEVICE_TAG;
    mCameraDevice.common.version = CAMERA_DEVICE_API_VERSION_3_2;
    mCameraDevice.common.close = close_camera_device;
    mCameraDevice.ops = &mCameraOps;
    mCameraDevice.priv = this;
    gCamCapability[cameraId]->version = CAM_HAL_V3;
    // TODO: hardcode for now until mctl add support for min_num_pp_bufs
    //TBD - To see if this hardcoding is needed. Check by printing if this is filled by mctl to 3
    gCamCapability[cameraId]->min_num_pp_bufs = 3;

    pthread_cond_init(&mRequestCond, NULL);
    mPendingRequest = 0;
    mCurrentRequestId = -1;
    pthread_mutex_init(&mMutex, NULL);

    for (size_t i = 0; i < CAMERA3_TEMPLATE_COUNT; i++)
        mDefaultMetadata[i] = NULL;

#ifdef HAS_MULTIMEDIA_HINTS
    if (hw_get_module(POWER_HARDWARE_MODULE_ID, (const hw_module_t **)&m_pPowerModule)) {
        ALOGE("%s: %s module not found", __func__, POWER_HARDWARE_MODULE_ID);
    }
#endif
}

/*===========================================================================
 * FUNCTION   : ~QCamera3HardwareInterface
 *
 * DESCRIPTION: destructor of QCamera3HardwareInterface
 *
 * PARAMETERS : none
 *
 * RETURN     : none
 *==========================================================================*/
QCamera3HardwareInterface::~QCamera3HardwareInterface()
{
    ALOGV("%s: E", __func__);
    /* We need to stop all streams before deleting any stream */
        /*flush the metadata list*/
    if (!mStoredMetadataList.empty()) {
        for (List<MetadataBufferInfo>::iterator m = mStoredMetadataList.begin();
              m != mStoredMetadataList.end(); ) {
            mMetadataChannel->bufDone(m->meta_buf);
            free(m->meta_buf);
            m = mStoredMetadataList.erase(m);
        }
    }

    // NOTE: 'camera3_stream_t *' objects are already freed at
    //        this stage by the framework
    for (List<stream_info_t *>::iterator it = mStreamInfo.begin();
        it != mStreamInfo.end(); it++) {
        QCamera3Channel *channel = (*it)->channel;
        if (channel) {
            channel->stop();
        }
    }

    for (List<stream_info_t *>::iterator it = mStreamInfo.begin();
        it != mStreamInfo.end(); it++) {
        QCamera3Channel *channel = (*it)->channel;
        if (channel)
            delete channel;
        free (*it);
    }

    mPictureChannel = NULL;

    if (mJpegSettings != NULL) {
        free(mJpegSettings);
        mJpegSettings = NULL;
    }

    /* Clean up all channels */
    if (mCameraInitialized) {
        if (mMetadataChannel) {
            mMetadataChannel->stop();
            delete mMetadataChannel;
            mMetadataChannel = NULL;
        }
        deinitParameters();
    }

    if (mCameraOpened)
        closeCamera();

    mPendingBuffersMap.mPendingBufferList.clear();
    mPendingRequestsList.clear();

    for (size_t i = 0; i < CAMERA3_TEMPLATE_COUNT; i++)
        if (mDefaultMetadata[i])
            free_camera_metadata(mDefaultMetadata[i]);

    pthread_cond_destroy(&mRequestCond);

    pthread_mutex_destroy(&mMutex);
    ALOGV("%s: X", __func__);
}

/*===========================================================================
 * FUNCTION   : openCamera
 *
 * DESCRIPTION: open camera
 *
 * PARAMETERS :
 *   @hw_device  : double ptr for camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera3HardwareInterface::openCamera(struct hw_device_t **hw_device)
{
    int rc = 0;
    pthread_mutex_lock(&mCameraSessionLock);
    if (mCameraSessionActive) {
        ALOGE("%s: multiple simultaneous camera instance not supported", __func__);
        pthread_mutex_unlock(&mCameraSessionLock);
        return -EUSERS;
    }

    if (mCameraOpened) {
        *hw_device = NULL;
        return PERMISSION_DENIED;
    }

    rc = openCamera();
    if (rc == 0) {
        *hw_device = &mCameraDevice.common;
        mCameraSessionActive = 1;
    } else
        *hw_device = NULL;

#ifdef HAS_MULTIMEDIA_HINTS
    if (rc == 0) {
        if (m_pPowerModule) {
            if (m_pPowerModule->powerHint) {
                m_pPowerModule->powerHint(m_pPowerModule, POWER_HINT_VIDEO_ENCODE,
                        (void *)"state=1");
            }
        }
    }
#endif
    pthread_mutex_unlock(&mCameraSessionLock);
    return rc;
}

/*===========================================================================
 * FUNCTION   : openCamera
 *
 * DESCRIPTION: open camera
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera3HardwareInterface::openCamera()
{
    if (mCameraHandle) {
        ALOGE("Failure: Camera already opened");
        return ALREADY_EXISTS;
    }
    mCameraHandle = camera_open(mCameraId);
    if (!mCameraHandle) {
        ALOGE("camera_open failed.");
        return UNKNOWN_ERROR;
    }

    mCameraOpened = true;

    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : closeCamera
 *
 * DESCRIPTION: close camera
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera3HardwareInterface::closeCamera()
{
    int rc = NO_ERROR;

    rc = mCameraHandle->ops->close_camera(mCameraHandle->camera_handle);
    mCameraHandle = NULL;
    mCameraOpened = false;

#ifdef HAS_MULTIMEDIA_HINTS
    if (rc == NO_ERROR) {
        if (m_pPowerModule) {
            if (m_pPowerModule->powerHint) {
                m_pPowerModule->powerHint(m_pPowerModule, POWER_HINT_VIDEO_ENCODE,
                        (void *)"state=0");
            }
        }
    }
#endif

    return rc;
}

/*===========================================================================
 * FUNCTION   : initialize
 *
 * DESCRIPTION: Initialize frameworks callback functions
 *
 * PARAMETERS :
 *   @callback_ops : callback function to frameworks
 *
 * RETURN     :
 *
 *==========================================================================*/
int QCamera3HardwareInterface::initialize(
        const struct camera3_callback_ops *callback_ops)
{
    int rc;

    pthread_mutex_lock(&mMutex);

    rc = initParameters();
    if (rc < 0) {
        ALOGE("%s: initParamters failed %d", __func__, rc);
       goto err1;
    }

    mCallbackOps = callback_ops;

    pthread_mutex_unlock(&mMutex);
    mCameraInitialized = true;
    return 0;

err1:
    pthread_mutex_unlock(&mMutex);
    return rc;
}

/*===========================================================================
 * FUNCTION   : configureStreams
 *
 * DESCRIPTION: Reset HAL camera device processing pipeline and set up new input
 *              and output streams.
 *
 * PARAMETERS :
 *   @stream_list : streams to be configured
 *
 * RETURN     :
 *
 *==========================================================================*/
int QCamera3HardwareInterface::configureStreams(
        camera3_stream_configuration_t *streamList)
{
    int rc = 0;
    mIsZslMode = false;
    // Sanity check stream_list
    if (streamList == NULL) {
        ALOGE("%s: NULL stream configuration", __func__);
        return BAD_VALUE;
    }

    if (streamList->streams == NULL) {
        ALOGE("%s: NULL stream list", __func__);
        return BAD_VALUE;
    }

    if (streamList->num_streams < 1) {
        ALOGE("%s: Bad number of streams requested: %d", __func__,
                streamList->num_streams);
        return BAD_VALUE;
    }

    camera3_stream_t *inputStream = NULL;
    camera3_stream_t *jpegStream = NULL;
    /* first invalidate all the steams in the mStreamList
     * if they appear again, they will be validated */
    for (List<stream_info_t*>::iterator it=mStreamInfo.begin();
            it != mStreamInfo.end(); it++) {
        QCamera3Channel *channel = (QCamera3Channel*)(*it)->stream->priv;
        channel->stop();
        (*it)->status = INVALID;
    }

    if (mMetadataChannel) {
        /* If content of mStreamInfo is not 0, there is metadata stream */
        mMetadataChannel->stop();
    }
    // Acquire Mutex after stoping all the channels
    pthread_mutex_lock(&mMutex);
    for (size_t i = 0; i < streamList->num_streams; i++) {
        camera3_stream_t *newStream = streamList->streams[i];
        ALOGD("%s: newStream type = %d, stream format = %d stream size : %d x %d",
                __func__, newStream->stream_type, newStream->format,
                 newStream->width, newStream->height);
        //if the stream is in the mStreamList validate it
        bool stream_exists = false;
        for (List<stream_info_t*>::iterator it=mStreamInfo.begin();
                it != mStreamInfo.end(); it++) {
            if ((*it)->stream == newStream) {
                QCamera3Channel *channel =
                    (QCamera3Channel*)(*it)->stream->priv;
                stream_exists = true;
                delete channel;
                (*it)->status = VALID;
                (*it)->stream->priv = NULL;
                (*it)->channel = NULL;
            }
        }
        if (!stream_exists) {
            //new stream
            stream_info_t* stream_info;
            stream_info = (stream_info_t* )malloc(sizeof(stream_info_t));
            stream_info->stream = newStream;
            stream_info->status = VALID;
            stream_info->channel = NULL;
            mStreamInfo.push_back(stream_info);
        }
        if (newStream->stream_type == CAMERA3_STREAM_INPUT
                || newStream->stream_type == CAMERA3_STREAM_BIDIRECTIONAL ) {
            if (inputStream != NULL) {
                ALOGE("%s: Multiple input streams requested!", __func__);
                pthread_mutex_unlock(&mMutex);
                return BAD_VALUE;
            }
            inputStream = newStream;
        }
        if (newStream->format == HAL_PIXEL_FORMAT_BLOB) {
            jpegStream = newStream;
        }
    }
    mInputStream = inputStream;

    /*clean up invalid streams*/
    for (List<stream_info_t*>::iterator it=mStreamInfo.begin();
            it != mStreamInfo.end();) {
        if(((*it)->status) == INVALID){
            QCamera3Channel *channel = (QCamera3Channel*)(*it)->stream->priv;
            delete channel;
            free(*it);
            it = mStreamInfo.erase(it);
        } else {
            it++;
        }
    }

    if (mMetadataChannel) {
        delete mMetadataChannel;
        mMetadataChannel = NULL;
    }

    //Create metadata channel and initialize it
    mMetadataChannel = new QCamera3MetadataChannel(mCameraHandle->camera_handle,
                    mCameraHandle->ops, captureResultCb,
                    &gCamCapability[mCameraId]->padding_info, this);
    if (mMetadataChannel == NULL) {
        ALOGE("%s: failed to allocate metadata channel", __func__);
        rc = -ENOMEM;
        pthread_mutex_unlock(&mMutex);
        return rc;
    }
    rc = mMetadataChannel->initialize();
    if (rc < 0) {
        ALOGE("%s: metadata channel initialization failed", __func__);
        delete mMetadataChannel;
        mMetadataChannel = NULL;
        pthread_mutex_unlock(&mMutex);
        return rc;
    }

    /* Allocate channel objects for the requested streams */
    for (size_t i = 0; i < streamList->num_streams; i++) {
        camera3_stream_t *newStream = streamList->streams[i];
        uint32_t stream_usage = newStream->usage;
        cam_stream_type_t stream_type;
        if (newStream->stream_type == CAMERA3_STREAM_BIDIRECTIONAL &&
            newStream->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED && jpegStream){
            stream_type = CAM_STREAM_TYPE_SNAPSHOT;
        } else {
           switch (newStream->format) {
           case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED :
              {
                 if (stream_usage & private_handle_t::PRIV_FLAGS_VIDEO_ENCODER) {
                    stream_type = CAM_STREAM_TYPE_VIDEO;
                 } else {
                    stream_type = CAM_STREAM_TYPE_PREVIEW;
                 }
              }
              break;
           case HAL_PIXEL_FORMAT_YCbCr_420_888:
              stream_type = CAM_STREAM_TYPE_CALLBACK;
              break;
           case HAL_PIXEL_FORMAT_BLOB:
              stream_type = CAM_STREAM_TYPE_NON_ZSL_SNAPSHOT;
              break;
           default:
              stream_type = CAM_STREAM_TYPE_DEFAULT;
              break;
           }
        }
        if (newStream->priv == NULL) {
            //New stream, construct channel
            switch (newStream->stream_type) {
            case CAMERA3_STREAM_INPUT:
                newStream->usage = GRALLOC_USAGE_HW_CAMERA_READ;
                break;
            case CAMERA3_STREAM_BIDIRECTIONAL:
                newStream->usage = GRALLOC_USAGE_HW_CAMERA_READ |
                    GRALLOC_USAGE_HW_CAMERA_WRITE;
                break;
            case CAMERA3_STREAM_OUTPUT:
                /* For video encoding stream, set read/write rarely
                 * flag so that they may be set to un-cached */
                if (newStream->usage & GRALLOC_USAGE_HW_VIDEO_ENCODER)
                    newStream->usage =
                         (GRALLOC_USAGE_SW_READ_RARELY |
                         GRALLOC_USAGE_SW_WRITE_RARELY |
                         GRALLOC_USAGE_HW_CAMERA_WRITE);
                else
                    newStream->usage = GRALLOC_USAGE_HW_CAMERA_WRITE;
                break;
            default:
                ALOGE("%s: Invalid stream_type %d", __func__, newStream->stream_type);
                break;
            }

            if (newStream->stream_type == CAMERA3_STREAM_OUTPUT ||
                    newStream->stream_type == CAMERA3_STREAM_BIDIRECTIONAL) {
                QCamera3Channel *channel = NULL;
                switch (newStream->format) {
                case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
                case HAL_PIXEL_FORMAT_YCbCr_420_888:
                    newStream->max_buffers = QCamera3RegularChannel::kMaxBuffers;
                    if (newStream->stream_type == CAMERA3_STREAM_BIDIRECTIONAL &&
                        jpegStream) {
                        mIsZslMode = true;
                    }
                    channel = new QCamera3RegularChannel(mCameraHandle->camera_handle,
                        mCameraHandle->ops, captureResultCb,
                        &gCamCapability[mCameraId]->padding_info,
                        this,
                        newStream,
                        (cam_stream_type_t) stream_type);
                    if (channel == NULL) {
                        ALOGE("%s: allocation of channel failed", __func__);
                        pthread_mutex_unlock(&mMutex);
                        return -ENOMEM;
                    }

                    newStream->priv = channel;
                    break;
                case HAL_PIXEL_FORMAT_BLOB:
                    newStream->max_buffers = QCamera3PicChannel::kMaxBuffers;
                    mPictureChannel = new QCamera3PicChannel(mCameraHandle->camera_handle,
                            mCameraHandle->ops, captureResultCb,
                            &gCamCapability[mCameraId]->padding_info, this, newStream);
                    if (mPictureChannel == NULL) {
                        ALOGE("%s: allocation of channel failed", __func__);
                        pthread_mutex_unlock(&mMutex);
                        return -ENOMEM;
                    }
                    newStream->priv = (QCamera3Channel*)mPictureChannel;
                    break;

                //TODO: Add support for app consumed format?
                default:
                    ALOGE("%s: not a supported format 0x%x", __func__, newStream->format);
                    break;
                }
            }

            for (List<stream_info_t*>::iterator it=mStreamInfo.begin();
                    it != mStreamInfo.end(); it++) {
                if ((*it)->stream == newStream) {
                    (*it)->channel = (QCamera3Channel*) newStream->priv;
                    break;
                }
            }
        } else {
            // Channel already exists for this stream
            // Do nothing for now
        }
    }

    /* Initialize mPendingRequestInfo and mPendnigBuffersMap */
    mPendingRequestsList.clear();
    // Initialize/Reset the pending buffers list
    mPendingBuffersMap.num_buffers = 0;
    mPendingBuffersMap.mPendingBufferList.clear();

    /*flush the metadata list*/
    if (!mStoredMetadataList.empty()) {
        for (List<MetadataBufferInfo>::iterator m = mStoredMetadataList.begin();
              m != mStoredMetadataList.end(); m++) {
            mMetadataChannel->bufDone(m->meta_buf);
            free(m->meta_buf);
            m = mStoredMetadataList.erase(m);
        }
    }

    //settings/parameters don't carry over for new configureStreams
    memset(mParameters, 0, sizeof(parm_buffer_t));
    mFirstRequest = true;

    pthread_mutex_unlock(&mMutex);
    return rc;
}

/*===========================================================================
 * FUNCTION   : validateCaptureRequest
 *
 * DESCRIPTION: validate a capture request from camera service
 *
 * PARAMETERS :
 *   @request : request from framework to process
 *
 * RETURN     :
 *
 *==========================================================================*/
int QCamera3HardwareInterface::validateCaptureRequest(
                    camera3_capture_request_t *request)
{
    ssize_t idx = 0;
    const camera3_stream_buffer_t *b;
    CameraMetadata meta;

    /* Sanity check the request */
    if (request == NULL) {
        ALOGE("%s: NULL capture request", __func__);
        return BAD_VALUE;
    }

    uint32_t frameNumber = request->frame_number;
    if (request->input_buffer != NULL &&
            request->input_buffer->stream != mInputStream) {
        ALOGE("%s: Request %d: Input buffer not from input stream!",
                __FUNCTION__, frameNumber);
        return BAD_VALUE;
    }
    if (request->num_output_buffers < 1 || request->output_buffers == NULL) {
        ALOGE("%s: Request %d: No output buffers provided!",
                __FUNCTION__, frameNumber);
        return BAD_VALUE;
    }
    if (request->input_buffer != NULL) {
        b = request->input_buffer;
        QCamera3Channel *channel =
            static_cast<QCamera3Channel*>(b->stream->priv);
        if (channel == NULL) {
            ALOGE("%s: Request %d: Buffer %d: Unconfigured stream!",
                    __func__, frameNumber, idx);
            return BAD_VALUE;
        }
        if (b->status != CAMERA3_BUFFER_STATUS_OK) {
            ALOGE("%s: Request %d: Buffer %d: Status not OK!",
                    __func__, frameNumber, idx);
            return BAD_VALUE;
        }
        if (b->release_fence != -1) {
            ALOGE("%s: Request %d: Buffer %d: Has a release fence!",
                    __func__, frameNumber, idx);
            return BAD_VALUE;
        }
        if (b->buffer == NULL) {
            ALOGE("%s: Request %d: Buffer %d: NULL buffer handle!",
                    __func__, frameNumber, idx);
            return BAD_VALUE;
        }
    }

    // Validate all buffers
    b = request->output_buffers;
    do {
        QCamera3Channel *channel =
                static_cast<QCamera3Channel*>(b->stream->priv);
        if (channel == NULL) {
            ALOGE("%s: Request %d: Buffer %d: Unconfigured stream!",
                    __func__, frameNumber, idx);
            return BAD_VALUE;
        }
        if (b->status != CAMERA3_BUFFER_STATUS_OK) {
            ALOGE("%s: Request %d: Buffer %d: Status not OK!",
                    __func__, frameNumber, idx);
            return BAD_VALUE;
        }
        if (b->release_fence != -1) {
            ALOGE("%s: Request %d: Buffer %d: Has a release fence!",
                    __func__, frameNumber, idx);
            return BAD_VALUE;
        }
        if (b->buffer == NULL) {
            ALOGE("%s: Request %d: Buffer %d: NULL buffer handle!",
                    __func__, frameNumber, idx);
            return BAD_VALUE;
        }
        idx++;
        b = request->output_buffers + idx;
    } while (idx < (ssize_t)request->num_output_buffers);

    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : handleMetadataWithLock
 *
 * DESCRIPTION: Handles metadata buffer callback with mMutex lock held.
 *
 * PARAMETERS : @metadata_buf: metadata buffer
 *
 * RETURN     :
 *
 *==========================================================================*/
void QCamera3HardwareInterface::handleMetadataWithLock(
    mm_camera_super_buf_t *metadata_buf)
{
    metadata_buffer_t *metadata = (metadata_buffer_t *)metadata_buf->bufs[0]->buffer;
    int32_t frame_number_valid = *(int32_t *)
        POINTER_OF(CAM_INTF_META_FRAME_NUMBER_VALID, metadata);
    uint32_t pending_requests = *(uint32_t *)POINTER_OF(
        CAM_INTF_META_PENDING_REQUESTS, metadata);
    uint32_t frame_number = *(uint32_t *)
        POINTER_OF(CAM_INTF_META_FRAME_NUMBER, metadata);
    const struct timeval *tv = (const struct timeval *)
        POINTER_OF(CAM_INTF_META_SENSOR_TIMESTAMP, metadata);
    nsecs_t capture_time = (nsecs_t)tv->tv_sec * NSEC_PER_SEC +
        tv->tv_usec * NSEC_PER_USEC;
    bool frame_number_exists = FALSE;

    if (!frame_number_valid) {
        ALOGV("%s: Not a valid frame number, used as SOF only", __func__);
        mMetadataChannel->bufDone(metadata_buf);
        free(metadata_buf);
        goto done_metadata;
    }
    ALOGV("%s: valid frame_number = %d, capture_time = %lld", __func__,
            frame_number, capture_time);

    // Go through the pending requests info and send shutter/results to frameworks
    for (List<PendingRequestInfo>::iterator i = mPendingRequestsList.begin();
        i != mPendingRequestsList.end() && i->frame_number <= frame_number;) {
        camera3_capture_result_t result;
	memset(&result, 0, sizeof(camera3_capture_result_t));
        camera3_notify_msg_t notify_msg;
        ALOGV("%s: frame_number in the list is %d", __func__, i->frame_number);
        frame_number_exists = TRUE; // This frame number exists in Pending list
        // Flush out all entries with less or equal frame numbers.

        //TODO: Make sure shutter timestamp really reflects shutter timestamp.
        //Right now it's the same as metadata timestamp

        //TODO: When there is metadata drop, how do we derive the timestamp of
        //dropped frames? For now, we fake the dropped timestamp by substracting
        //from the reported timestamp
        nsecs_t current_capture_time = capture_time -
            (frame_number - i->frame_number) * NSEC_PER_33MSEC;

        // Send shutter notify to frameworks
        notify_msg.type = CAMERA3_MSG_SHUTTER;
        notify_msg.message.shutter.frame_number = i->frame_number;
        notify_msg.message.shutter.timestamp = current_capture_time;
        mCallbackOps->notify(mCallbackOps, &notify_msg);
        ALOGV("%s: notify frame_number = %d, capture_time = %lld", __func__,
                i->frame_number, capture_time);
        mPendingRequest--;

        // Send empty metadata with already filled buffers for dropped metadata
        // and send valid metadata with already filled buffers for current metadata
        if (i->frame_number < frame_number) {
            CameraMetadata dummyMetadata;
            dummyMetadata.update(ANDROID_SENSOR_TIMESTAMP,
                    &current_capture_time, 1);
            dummyMetadata.update(ANDROID_REQUEST_ID,
                    &(i->request_id), 1);
            result.result = dummyMetadata.release();
        } else {
            result.result = translateCbMetadataToResultMetadata(metadata,
                    current_capture_time, i->request_id, i->ae_trigger,
                    i->pipeline_depth);
            if (mIsZslMode) {
                int found_metadata = 0;
                //for ZSL case store the metadata buffer and corresp. ZSL handle ptr
                for (List<RequestedBufferInfo>::iterator j = i->buffers.begin();
                    j != i->buffers.end(); j++) {
                    if (j->stream->stream_type == CAMERA3_STREAM_BIDIRECTIONAL) {
                        //check if corresp. zsl already exists in the stored metadata list
                        for (List<MetadataBufferInfo>::iterator m = mStoredMetadataList.begin();
                                m != mStoredMetadataList.begin(); m++) {
                            if (m->frame_number == frame_number) {
                                m->meta_buf = metadata_buf;
                                found_metadata = 1;
                                break;
                            }
                        }
                        if (!found_metadata) {
                            MetadataBufferInfo store_meta_info;
                            store_meta_info.meta_buf = metadata_buf;
                            store_meta_info.frame_number = frame_number;
                            mStoredMetadataList.push_back(store_meta_info);
                            found_metadata = 1;
                        }
                    }
                }
                if (!found_metadata) {
                    if (!i->input_buffer_present && i->blob_request) {
                        //livesnapshot or fallback non-zsl snapshot case
                        for (List<RequestedBufferInfo>::iterator j = i->buffers.begin();
                                j != i->buffers.end(); j++){
                            if (j->stream->stream_type == CAMERA3_STREAM_OUTPUT &&
                                j->stream->format == HAL_PIXEL_FORMAT_BLOB) {
                                mPictureChannel->queueMetadata(metadata_buf,mMetadataChannel,true);
                                break;
                            }
                        }
                    } else {
                        //return the metadata immediately
                        mMetadataChannel->bufDone(metadata_buf);
                        free(metadata_buf);
                    }
                }
            } else if (!mIsZslMode && i->blob_request) {
                //If it is a blob request then send the metadata to the picture channel
                mPictureChannel->queueMetadata(metadata_buf,mMetadataChannel,true);
            } else {
                // Return metadata buffer
                mMetadataChannel->bufDone(metadata_buf);
                free(metadata_buf);
            }
        }
        if (!result.result) {
            ALOGE("%s: metadata is NULL", __func__);
        }
        result.frame_number = i->frame_number;
        result.num_output_buffers = 0;
        result.output_buffers = NULL;
        result.input_buffer = NULL;
        result.partial_result = 1;
        for (List<RequestedBufferInfo>::iterator j = i->buffers.begin();
                    j != i->buffers.end(); j++) {
            if (j->buffer) {
                result.num_output_buffers++;
            }
        }

        if (result.num_output_buffers > 0) {
            camera3_stream_buffer_t *result_buffers =
                new camera3_stream_buffer_t[result.num_output_buffers];
            if (!result_buffers) {
                ALOGE("%s: Fatal error: out of memory", __func__);
            }
            size_t result_buffers_idx = 0;
            for (List<RequestedBufferInfo>::iterator j = i->buffers.begin();
                 j != i->buffers.end(); j++) {
                if (j->buffer) {
                    for (List<PendingBufferInfo>::iterator k =
                      mPendingBuffersMap.mPendingBufferList.begin();
                      k != mPendingBuffersMap.mPendingBufferList.end(); k++) {
                      if (k->buffer == j->buffer->buffer) {
                        ALOGV("%s: Found buffer %p in pending buffer List "
                              "for frame %d, Take it out!!", __func__,
                               k->buffer, k->frame_number);
                        mPendingBuffersMap.num_buffers--;
                        k = mPendingBuffersMap.mPendingBufferList.erase(k);
                        break;
                      }
                    }

                    result_buffers[result_buffers_idx++] = *(j->buffer);
                    free(j->buffer);
                    j->buffer = NULL;
                }
            }
            result.output_buffers = result_buffers;

            mCallbackOps->process_capture_result(mCallbackOps, &result);
            ALOGV("%s: meta frame_number = %d, capture_time = %lld",
                    __func__, result.frame_number, current_capture_time);
            free_camera_metadata((camera_metadata_t *)result.result);
            delete[] result_buffers;
        } else {
            mCallbackOps->process_capture_result(mCallbackOps, &result);
            ALOGV("%s: meta frame_number = %d, capture_time = %lld",
                        __func__, result.frame_number, current_capture_time);
            free_camera_metadata((camera_metadata_t *)result.result);
        }
        // erase the element from the list
        i = mPendingRequestsList.erase(i);
    }
    if (!frame_number_exists) {
        ALOGD("%s: Frame number# %d not in the Pending Request list", __func__,
                frame_number);
        // Race condition where in Metadata Frame# is valid but its not in Pending list
        mMetadataChannel->bufDone(metadata_buf);
        free(metadata_buf);
    }

done_metadata:
    for (List<PendingRequestInfo>::iterator i = mPendingRequestsList.begin();
        i != mPendingRequestsList.end() ;i++) {
        i->pipeline_depth++;
    }
    if (!pending_requests)
        unblockRequestIfNecessary();

}

/*===========================================================================
 * FUNCTION   : handleBufferWithLock
 *
 * DESCRIPTION: Handles image buffer callback with mMutex lock held.
 *
 * PARAMETERS : @buffer: image buffer for the callback
 *              @frame_number: frame number of the image buffer
 *
 * RETURN     :
 *
 *==========================================================================*/
void QCamera3HardwareInterface::handleBufferWithLock(
    camera3_stream_buffer_t *buffer, uint32_t frame_number)
{
    // If the frame number doesn't exist in the pending request list,
    // directly send the buffer to the frameworks, and update pending buffers map
    // Otherwise, book-keep the buffer.
    List<PendingRequestInfo>::iterator i = mPendingRequestsList.begin();
    while (i != mPendingRequestsList.end() && i->frame_number != frame_number){
        i++;
    }
    if (i == mPendingRequestsList.end()) {
        // Verify all pending requests frame_numbers are greater
        for (List<PendingRequestInfo>::iterator j = mPendingRequestsList.begin();
                j != mPendingRequestsList.end(); j++) {
            if (j->frame_number < frame_number) {
                ALOGE("%s: Error: pending frame number %d is smaller than %d",
                        __func__, j->frame_number, frame_number);
            }
        }
        camera3_capture_result_t result;
        memset(&result, 0, sizeof(camera3_capture_result_t));
        result.result = NULL;
        result.frame_number = frame_number;
        result.num_output_buffers = 1;
        result.output_buffers = buffer;
        result.input_buffer = NULL;
        result.partial_result = 1;
        ALOGV("%s: result frame_number = %d, buffer = %p",
                __func__, frame_number, buffer->buffer);

        for (List<PendingBufferInfo>::iterator k =
                mPendingBuffersMap.mPendingBufferList.begin();
                k != mPendingBuffersMap.mPendingBufferList.end(); k++ ) {
            if (k->buffer == buffer->buffer) {
                ALOGV("%s: Found Frame buffer, take it out from list",
                        __func__);

                mPendingBuffersMap.num_buffers--;
                k = mPendingBuffersMap.mPendingBufferList.erase(k);
                break;
            }
        }
        ALOGV("%s: mPendingBuffersMap.num_buffers = %d",
            __func__, mPendingBuffersMap.num_buffers);

        if (buffer->stream->stream_type == CAMERA3_STREAM_BIDIRECTIONAL) {
            int found = 0;
            for (List<MetadataBufferInfo>::iterator k = mStoredMetadataList.begin();
                k != mStoredMetadataList.end(); k++) {
                if (k->frame_number == frame_number) {
                    k->zsl_buf_hdl = buffer->buffer;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                MetadataBufferInfo meta_info;
                meta_info.frame_number = frame_number;
                meta_info.zsl_buf_hdl = buffer->buffer;
                mStoredMetadataList.push_back(meta_info);
            }
        }
        mCallbackOps->process_capture_result(mCallbackOps, &result);
    } else {
        for (List<RequestedBufferInfo>::iterator j = i->buffers.begin();
                j != i->buffers.end(); j++) {
            if (j->stream == buffer->stream) {
                if (j->buffer != NULL) {
                    ALOGE("%s: Error: buffer is already set", __func__);
                } else {
                    j->buffer = (camera3_stream_buffer_t *)malloc(
                            sizeof(camera3_stream_buffer_t));
                    *(j->buffer) = *buffer;
                    ALOGV("%s: cache buffer %p at result frame_number %d",
                            __func__, buffer, frame_number);
                }
            }
        }
    }
}

/*===========================================================================
 * FUNCTION   : unblockRequestIfNecessary
 *
 * DESCRIPTION: Unblock capture_request if max_buffer hasn't been reached. Note
 *              that mMutex is held when this function is called.
 *
 * PARAMETERS :
 *
 * RETURN     :
 *
 *==========================================================================*/
void QCamera3HardwareInterface::unblockRequestIfNecessary()
{
    bool max_buffers_dequeued = false;

    uint32_t queued_buffers = 0;
    for(List<stream_info_t*>::iterator it=mStreamInfo.begin();
        it != mStreamInfo.end(); it++) {
        queued_buffers = 0;
        for (List<PendingBufferInfo>::iterator k =
            mPendingBuffersMap.mPendingBufferList.begin();
            k != mPendingBuffersMap.mPendingBufferList.end(); k++ ) {
            if (k->stream == (*it)->stream)
                queued_buffers++;

            ALOGV("%s: Dequeued %d buffers for stream %p", __func__,
                queued_buffers, (*it)->stream);
            if (queued_buffers >=(* it)->stream->max_buffers) {
                ALOGV("%s: Wait!!! Max buffers Dequed", __func__);
                max_buffers_dequeued = true;
                break;
            }
        }
    }

    if (!max_buffers_dequeued) {
        // Unblock process_capture_request
        pthread_cond_signal(&mRequestCond);
    }
}

/*===========================================================================
 * FUNCTION   : registerStreamBuffers
 *
 * DESCRIPTION: Register buffers for a given stream with the HAL device.
 *
 * PARAMETERS :
 *   @stream_list : streams to be configured
 *
 * RETURN     :
 *
 *==========================================================================*/
int QCamera3HardwareInterface::registerStreamBuffers(
        const camera3_stream_buffer_set_t * /*buffer_set*/)
{
    //Deprecated
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : processCaptureRequest
 *
 * DESCRIPTION: process a capture request from camera service
 *
 * PARAMETERS :
 *   @request : request from framework to process
 *
 * RETURN     :
 *
 *==========================================================================*/
int QCamera3HardwareInterface::processCaptureRequest(
                    camera3_capture_request_t *request)
{
    int rc = NO_ERROR;
    int32_t request_id;
    CameraMetadata meta;
    MetadataBufferInfo reproc_meta;
    int queueMetadata = 0;

    pthread_mutex_lock(&mMutex);

    rc = validateCaptureRequest(request);
    if (rc != NO_ERROR) {
        ALOGE("%s: incoming request is not valid", __func__);
        pthread_mutex_unlock(&mMutex);
        return -EINVAL;
    }

    if (mFirstRequest) {
        for (size_t i = 0; i < request->num_output_buffers; i++) {
            const camera3_stream_buffer_t& output = request->output_buffers[i];
            QCamera3Channel *channel = (QCamera3Channel *)output.stream->priv;
            rc = channel->registerBuffer(output.buffer);
            if (rc < 0) {
                ALOGE("%s: registerBuffer failed",
                        __func__);
                pthread_mutex_unlock(&mMutex);
                return -ENODEV;
            }
        }

        for (List<stream_info_t *>::iterator it = mStreamInfo.begin();
            it != mStreamInfo.end(); it++) {
            QCamera3Channel *channel = (QCamera3Channel *)(*it)->stream->priv;
            rc = channel->initialize();
            if (NO_ERROR != rc) {
                ALOGE("%s : Channel initialization failed %d", __func__, rc);
                mMetadataChannel->stop();
                pthread_mutex_unlock(&mMutex);
                return -ENODEV;
            }
        }
    }

    uint32_t frameNumber = request->frame_number;
    uint32_t streamTypeMask = 0;

    meta = request->settings;
    if (meta.exists(ANDROID_REQUEST_ID)) {
        request_id = meta.find(ANDROID_REQUEST_ID).data.i32[0];
        mCurrentRequestId = request_id;
        ALOGV("%s: Received request with id: %d",__func__, request_id);
    } else if (mFirstRequest || mCurrentRequestId == -1){
        ALOGE("%s: Unable to find request id field, \
                & no previous id available", __func__);
        pthread_mutex_unlock(&mMutex);
        return -EINVAL;
    } else {
        ALOGV("%s: Re-using old request id", __func__);
        request_id = mCurrentRequestId;
    }

    ALOGV("%s: %d, num_output_buffers = %d input_buffer = %p frame_number = %d",
                                    __func__, __LINE__,
                                    request->num_output_buffers,
                                    request->input_buffer,
                                    frameNumber);
    // Acquire all request buffers first
    int blob_request = 0;
    for (size_t i = 0; i < request->num_output_buffers; i++) {
        const camera3_stream_buffer_t& output = request->output_buffers[i];
        QCamera3Channel *channel = (QCamera3Channel *)output.stream->priv;
        sp<Fence> acquireFence = new Fence(output.acquire_fence);

        if (output.stream->format == HAL_PIXEL_FORMAT_BLOB) {
        //Call function to store local copy of jpeg data for encode params.
            blob_request = 1;
            rc = getJpegSettings(request->settings);
            if (rc < 0) {
                ALOGE("%s: failed to get jpeg parameters", __func__);
                pthread_mutex_unlock(&mMutex);
                return rc;
            }
        }

        rc = acquireFence->wait(Fence::TIMEOUT_NEVER);
        if (rc != OK) {
            ALOGE("%s: fence wait failed %d", __func__, rc);
            pthread_mutex_unlock(&mMutex);
            return rc;
        }
        streamTypeMask |= channel->getStreamTypeMask();
    }

    PendingRequestInfo pendingRequest;
    pendingRequest.frame_number = frameNumber;
    pendingRequest.num_buffers = request->num_output_buffers;
    pendingRequest.request_id = request_id;
    pendingRequest.blob_request = blob_request;
    pendingRequest.input_buffer_present = (request->input_buffer != NULL)? 1 : 0;
    pendingRequest.pipeline_depth = 0;
    pendingRequest.ae_trigger.trigger_id = mPrecaptureId;
    pendingRequest.ae_trigger.trigger = CAM_AEC_TRIGGER_IDLE;

    rc = setFrameParameters(request->frame_number, request->settings,
            streamTypeMask, pendingRequest.ae_trigger);
    if (rc < 0) {
        ALOGE("%s: fail to set frame parameters", __func__);
        pthread_mutex_unlock(&mMutex);
        return rc;
    }

    for (size_t i = 0; i < request->num_output_buffers; i++) {
        RequestedBufferInfo requestedBuf;
        requestedBuf.stream = request->output_buffers[i].stream;
        requestedBuf.buffer = NULL;
        pendingRequest.buffers.push_back(requestedBuf);

        // Add to buffer handle the pending buffers list
        PendingBufferInfo bufferInfo;
        bufferInfo.frame_number = frameNumber;
        bufferInfo.buffer = request->output_buffers[i].buffer;
        bufferInfo.stream = request->output_buffers[i].stream;
        mPendingBuffersMap.mPendingBufferList.push_back(bufferInfo);
        mPendingBuffersMap.num_buffers++;
        ALOGV("%s: frame = %d, buffer = %p, stream = %p, stream format = %d",
          __func__, frameNumber, bufferInfo.buffer, bufferInfo.stream,
          bufferInfo.stream->format);
    }
    ALOGV("%s: mPendingBuffersMap.num_buffers = %d",
          __func__, mPendingBuffersMap.num_buffers);
    mPendingRequestsList.push_back(pendingRequest);

    // Notify metadata channel we receive a request
    mMetadataChannel->request(NULL, frameNumber);

    // Call request on other streams
    for (size_t i = 0; i < request->num_output_buffers; i++) {
        const camera3_stream_buffer_t& output = request->output_buffers[i];
        QCamera3Channel *channel = (QCamera3Channel *)output.stream->priv;
        mm_camera_buf_def_t *pInputBuffer = NULL;

        if (channel == NULL) {
            ALOGE("%s: invalid channel pointer for stream", __func__);
            continue;
        }

        if (output.stream->format == HAL_PIXEL_FORMAT_BLOB) {
            QCamera3RegularChannel* inputChannel = NULL;
            if(request->input_buffer != NULL){
                //Try to get the internal format
                inputChannel = (QCamera3RegularChannel*)
                    request->input_buffer->stream->priv;
                if(inputChannel == NULL ){
                    ALOGE("%s: failed to get input channel handle", __func__);
                } else {
                    pInputBuffer =
                        inputChannel->getInternalFormatBuffer(
                                request->input_buffer->buffer);
                    ALOGD("%s: Input buffer dump",__func__);
                    ALOGD("Stream id: %d", pInputBuffer->stream_id);
                    ALOGD("streamtype:%d", pInputBuffer->stream_type);
                    ALOGD("frame len:%d", pInputBuffer->frame_len);
                    ALOGD("Handle:%p", request->input_buffer->buffer);
                    //TODO: need to get corresponding metadata and send it to pproc
                    for (List<MetadataBufferInfo>::iterator m = mStoredMetadataList.begin();
                         m != mStoredMetadataList.end(); m++) {
                        if (m->zsl_buf_hdl == request->input_buffer->buffer) {
                            reproc_meta.meta_buf = m->meta_buf;
                            queueMetadata = 1;
                            break;
                        }
                    }
                }
            }
            rc = channel->request(output.buffer, frameNumber, mJpegSettings,
                            pInputBuffer,(QCamera3Channel*)inputChannel);
            if (queueMetadata) {
                mPictureChannel->queueMetadata(reproc_meta.meta_buf,mMetadataChannel,false);
            }
        } else {
            ALOGV("%s: %d, request with buffer %p, frame_number %d", __func__,
                __LINE__, output.buffer, frameNumber);
            if (mIsZslMode && output.stream->stream_type == CAMERA3_STREAM_BIDIRECTIONAL) {
                for (List<MetadataBufferInfo>::iterator m = mStoredMetadataList.begin();
                     m != mStoredMetadataList.end(); m++) {
                   for (uint32_t j = 0; j < request->num_output_buffers; j++) {
                        if (m->zsl_buf_hdl == request->output_buffers[j].buffer) {
                            mMetadataChannel->bufDone(m->meta_buf);
                            free(m->meta_buf);
                            m = mStoredMetadataList.erase(m);
                            break;
                        }
                   }
                }
            }
            rc = channel->request(output.buffer, frameNumber);
        }
        if (rc < 0) {
            ALOGE("%s: Fail to issue channel request", __func__);
            pthread_mutex_unlock(&mMutex);
            return -ENODEV;
        }
    }

    mFirstRequest = false;

    //Block on conditional variable
    mPendingRequest++;
    do {
        pthread_cond_wait(&mRequestCond, &mMutex);
    }while (mPendingRequest >= kMaxInFlight);

    pthread_mutex_unlock(&mMutex);

    return rc;
}

/*===========================================================================
 * FUNCTION   : getMetadataVendorTagOps
 *
 * DESCRIPTION:
 *
 * PARAMETERS :
 *
 *
 * RETURN     :
 *==========================================================================*/
void QCamera3HardwareInterface::getMetadataVendorTagOps(
                    vendor_tag_query_ops_t* /*ops*/)
{
    /* Enable locks when we eventually add Vendor Tags */
    /*
    pthread_mutex_lock(&mMutex);

    pthread_mutex_unlock(&mMutex);
    */
    return;
}

/*===========================================================================
 * FUNCTION   : dump
 *
 * DESCRIPTION:
 *
 * PARAMETERS :
 *
 *
 * RETURN     :
 *==========================================================================*/
void QCamera3HardwareInterface::dump(int /*fd*/)
{
    /*Enable lock when we implement this function*/
    /*
    pthread_mutex_lock(&mMutex);

    pthread_mutex_unlock(&mMutex);
    */
    return;
}

/*===========================================================================
 * FUNCTION   : flush
 *
 * DESCRIPTION:
 *
 * PARAMETERS :
 *
 *
 * RETURN     :
 *==========================================================================*/
int QCamera3HardwareInterface::flush()
{
    unsigned int frameNum = 0;
    camera3_notify_msg_t notify_msg;
    camera3_capture_result_t result;
    camera3_stream_buffer_t *pStream_Buf = NULL;
    FlushMap flushMap;

    ALOGV("%s: Unblocking Process Capture Request", __func__);

    memset(&result, 0, sizeof(camera3_capture_result_t));

    // Stop the Streams/Channels
    for (List<stream_info_t *>::iterator it = mStreamInfo.begin();
        it != mStreamInfo.end(); it++) {
        QCamera3Channel *channel = (QCamera3Channel *)(*it)->stream->priv;
        channel->stop();
        (*it)->status = INVALID;
    }

    if (mMetadataChannel) {
        /* If content of mStreamInfo is not 0, there is metadata stream */
        mMetadataChannel->stop();
    }

    // Mutex Lock
    pthread_mutex_lock(&mMutex);

    // Unblock process_capture_request
    mPendingRequest = 0;
    pthread_cond_signal(&mRequestCond);

    List<PendingRequestInfo>::iterator i = mPendingRequestsList.begin();
    frameNum = i->frame_number;
    ALOGV("%s: Oldest frame num on  mPendingRequestsList = %d",
      __func__, frameNum);

    // Go through the pending buffers and group them depending
    // on frame number
    for (List<PendingBufferInfo>::iterator k =
            mPendingBuffersMap.mPendingBufferList.begin();
            k != mPendingBuffersMap.mPendingBufferList.end();) {

        if (k->frame_number < frameNum) {
            ssize_t idx = flushMap.indexOfKey(k->frame_number);
            if (idx == NAME_NOT_FOUND) {
                Vector<PendingBufferInfo> pending;
                pending.add(*k);
                flushMap.add(k->frame_number, pending);
            } else {
                Vector<PendingBufferInfo> &pending =
                        flushMap.editValueFor(k->frame_number);
                pending.add(*k);
            }

            mPendingBuffersMap.num_buffers--;
            k = mPendingBuffersMap.mPendingBufferList.erase(k);
        } else {
            k++;
        }
    }

    for (size_t i = 0; i < flushMap.size(); i++) {
        uint32_t frame_number = flushMap.keyAt(i);
        const Vector<PendingBufferInfo> &pending = flushMap.valueAt(i);

        // Send Error notify to frameworks for each buffer for which
        // metadata buffer is already sent
        ALOGV("%s: Sending ERROR BUFFER for frame %d number of buffer %d",
          __func__, frame_number, pending.size());

        pStream_Buf = new camera3_stream_buffer_t[pending.size()];
        if (NULL == pStream_Buf) {
            ALOGE("%s: No memory for pending buffers array", __func__);
            pthread_mutex_unlock(&mMutex);
            return NO_MEMORY;
        }

        for (size_t j = 0; j < pending.size(); j++) {
            const PendingBufferInfo &info = pending.itemAt(j);
            notify_msg.type = CAMERA3_MSG_ERROR;
            notify_msg.message.error.error_code = CAMERA3_MSG_ERROR_BUFFER;
            notify_msg.message.error.error_stream = info.stream;
            notify_msg.message.error.frame_number = frame_number;
            pStream_Buf[j].acquire_fence = -1;
            pStream_Buf[j].release_fence = -1;
            pStream_Buf[j].buffer = info.buffer;
            pStream_Buf[j].status = CAMERA3_BUFFER_STATUS_ERROR;
            pStream_Buf[j].stream = info.stream;
            mCallbackOps->notify(mCallbackOps, &notify_msg);
            ALOGV("%s: notify frame_number = %d stream %p", __func__,
                    frame_number, info.stream);
        }

        result.result = NULL;
        result.frame_number = frame_number;
        result.num_output_buffers = pending.size();
        result.output_buffers = pStream_Buf;
        mCallbackOps->process_capture_result(mCallbackOps, &result);

        delete [] pStream_Buf;
    }

    ALOGV("%s:Sending ERROR REQUEST for all pending requests", __func__);

    flushMap.clear();
    for (List<PendingBufferInfo>::iterator k =
            mPendingBuffersMap.mPendingBufferList.begin();
            k != mPendingBuffersMap.mPendingBufferList.end();) {
        ssize_t idx = flushMap.indexOfKey(k->frame_number);
        if (idx == NAME_NOT_FOUND) {
            Vector<PendingBufferInfo> pending;
            pending.add(*k);
            flushMap.add(k->frame_number, pending);
        } else {
            Vector<PendingBufferInfo> &pending =
                    flushMap.editValueFor(k->frame_number);
            pending.add(*k);
        }

        mPendingBuffersMap.num_buffers--;
        k = mPendingBuffersMap.mPendingBufferList.erase(k);
    }

    // Go through the pending requests info and send error request to framework
    for (size_t i = 0; i < flushMap.size(); i++) {
        uint32_t frame_number = flushMap.keyAt(i);
        const Vector<PendingBufferInfo> &pending = flushMap.valueAt(i);
        ALOGV("%s:Sending ERROR REQUEST for frame %d",
              __func__, frame_number);

        // Send shutter notify to frameworks
        notify_msg.type = CAMERA3_MSG_ERROR;
        notify_msg.message.error.error_code = CAMERA3_MSG_ERROR_REQUEST;
        notify_msg.message.error.error_stream = NULL;
        notify_msg.message.error.frame_number = frame_number;
        mCallbackOps->notify(mCallbackOps, &notify_msg);

        pStream_Buf = new camera3_stream_buffer_t[pending.size()];
        if (NULL == pStream_Buf) {
            ALOGE("%s: No memory for pending buffers array", __func__);
            pthread_mutex_unlock(&mMutex);
            return NO_MEMORY;
        }

        for (size_t j = 0; j < pending.size(); j++) {
            const PendingBufferInfo &info = pending.itemAt(j);
            pStream_Buf[j].acquire_fence = -1;
            pStream_Buf[j].release_fence = -1;
            pStream_Buf[j].buffer = info.buffer;
            pStream_Buf[j].status = CAMERA3_BUFFER_STATUS_ERROR;
            pStream_Buf[j].stream = info.stream;
        }

        result.num_output_buffers = pending.size();
        result.output_buffers = pStream_Buf;
        result.result = NULL;
        result.frame_number = frame_number;
        mCallbackOps->process_capture_result(mCallbackOps, &result);
        delete [] pStream_Buf;
    }

    /* Reset pending buffer list and requests list */
    mPendingRequestsList.clear();

    flushMap.clear();
    mPendingBuffersMap.num_buffers = 0;
    mPendingBuffersMap.mPendingBufferList.clear();
    ALOGV("%s: Cleared all the pending buffers ", __func__);

    /*flush the metadata list*/
    if (!mStoredMetadataList.empty()) {
        for (List<MetadataBufferInfo>::iterator m = mStoredMetadataList.begin();
              m != mStoredMetadataList.end(); ) {
            mMetadataChannel->bufDone(m->meta_buf);
            free(m->meta_buf);
            m = mStoredMetadataList.erase(m);
        }
    }
    ALOGV("%s: Flushing the metadata list done!! ", __func__);

    mFirstRequest = true;
    pthread_mutex_unlock(&mMutex);
    return 0;
}

/*===========================================================================
 * FUNCTION   : captureResultCb
 *
 * DESCRIPTION: Callback handler for all capture result
 *              (streams, as well as metadata)
 *
 * PARAMETERS :
 *   @metadata : metadata information
 *   @buffer   : actual gralloc buffer to be returned to frameworks.
 *               NULL if metadata.
 *
 * RETURN     : NONE
 *==========================================================================*/
void QCamera3HardwareInterface::captureResultCb(mm_camera_super_buf_t *metadata_buf,
                camera3_stream_buffer_t *buffer, uint32_t frame_number)
{
    pthread_mutex_lock(&mMutex);

    if (metadata_buf)
        handleMetadataWithLock(metadata_buf);
    else
        handleBufferWithLock(buffer, frame_number);

    pthread_mutex_unlock(&mMutex);
    return;
}

/*===========================================================================
 * FUNCTION   : translateCbMetadataToResultMetadata
 *
 * DESCRIPTION:
 *
 * PARAMETERS :
 *   @metadata : metadata information from callback
 *
 * RETURN     : camera_metadata_t*
 *              metadata in a format specified by fwk
 *==========================================================================*/
camera_metadata_t*
QCamera3HardwareInterface::translateCbMetadataToResultMetadata
                                (metadata_buffer_t *metadata, nsecs_t timestamp,
                                 int32_t request_id, const cam_trigger_t &aeTrigger,
                                 uint8_t pipeline_depth)
{
    CameraMetadata camMetadata;
    camera_metadata_t* resultMetadata;

    camMetadata.update(ANDROID_SENSOR_TIMESTAMP, &timestamp, 1);
    camMetadata.update(ANDROID_REQUEST_ID, &request_id, 1);
    camMetadata.update(ANDROID_REQUEST_PIPELINE_DEPTH, &pipeline_depth, 1);

    /*CAM_INTF_META_HISTOGRAM - TODO*/
    /*cam_hist_stats_t  *histogram =
      (cam_hist_stats_t *)POINTER_OF(CAM_INTF_META_HISTOGRAM,
      metadata);*/

    /*face detection*/
    cam_face_detection_data_t *faceDetectionInfo =(cam_face_detection_data_t *)
        POINTER_OF(CAM_INTF_META_FACE_DETECTION, metadata);
    uint8_t numFaces = faceDetectionInfo->num_faces_detected;
    int32_t faceIds[numFaces];
    uint8_t faceScores[numFaces];
    int32_t faceRectangles[numFaces * 4];
    int32_t faceLandmarks[numFaces * 6];
    int j = 0, k = 0;
    for (int i = 0; i < numFaces; i++) {
        faceIds[i] = faceDetectionInfo->faces[i].face_id;
        faceScores[i] = faceDetectionInfo->faces[i].score;
        convertToRegions(faceDetectionInfo->faces[i].face_boundary,
                faceRectangles+j, -1);
        convertLandmarks(faceDetectionInfo->faces[i], faceLandmarks+k);
        j+= 4;
        k+= 6;
    }
    if (numFaces > 0) {
        camMetadata.update(ANDROID_STATISTICS_FACE_IDS, faceIds, numFaces);
        camMetadata.update(ANDROID_STATISTICS_FACE_SCORES, faceScores, numFaces);
        camMetadata.update(ANDROID_STATISTICS_FACE_RECTANGLES,
            faceRectangles, numFaces*4);
        camMetadata.update(ANDROID_STATISTICS_FACE_LANDMARKS,
            faceLandmarks, numFaces*6);
    }

    //uint8_t  *color_correct_mode =
    //    (uint8_t *)POINTER_OF(CAM_INTF_META_COLOR_CORRECT_MODE, metadata);
    camMetadata.update(ANDROID_COLOR_CORRECTION_MODE, &mColorCorrectMode, 1);
    ALOGI("Getting ANDROID_COLOR_CORRECTION_MODE=%d", mColorCorrectMode);

    camMetadata.update(ANDROID_CONTROL_AE_PRECAPTURE_ID,
            &aeTrigger.trigger_id, 1);

    /*aec regions*/
    cam_area_t  *hAeRegions =
        (cam_area_t *)POINTER_OF(CAM_INTF_META_AEC_ROI, metadata);
    int32_t aeRegions[5];
    convertToRegions(hAeRegions->rect, aeRegions, hAeRegions->weight);
    camMetadata.update(ANDROID_CONTROL_AE_REGIONS, aeRegions, 5);

    uint8_t ae_state =
        *(uint8_t *)POINTER_OF(CAM_INTF_META_AEC_STATE, metadata);
    //Override AE state for front(YUV) sensor if corresponding request
    //contain a precapture trigger. This is to work around the precapture
    //trigger timeout for YUV sensor.
    if (gCamCapability[mCameraId]->position == CAM_POSITION_FRONT &&
            aeTrigger.trigger_id > 0 && aeTrigger.trigger ==
            ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_START) {
        ae_state = ANDROID_CONTROL_AE_STATE_CONVERGED;
    }
    camMetadata.update(ANDROID_CONTROL_AE_STATE, &ae_state, 1);

    camMetadata.update(ANDROID_CONTROL_AE_LOCK, &mAeLock, 1);

    int32_t  *expCompensation =
      (int32_t *)POINTER_OF(CAM_INTF_PARM_EXPOSURE_COMPENSATION, metadata);
    camMetadata.update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
                                  expCompensation, 1);

    int32_t fps_range[2];
    cam_fps_range_t * float_range =
      (cam_fps_range_t *)POINTER_OF(CAM_INTF_PARM_FPS_RANGE, metadata);
    fps_range[0] = (int32_t)float_range->min_fps;
    fps_range[1] = (int32_t)float_range->max_fps;
    camMetadata.update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, fps_range, 2);

    //uint8_t  *focusMode =
    //    (uint8_t *)POINTER_OF(CAM_INTF_PARM_FOCUS_MODE, metadata);
    camMetadata.update(ANDROID_CONTROL_AF_MODE, &mAfMode, 1);

    /*af regions*/
    if (gCamCapability[mCameraId]->supported_focus_modes_cnt > 1) {
        cam_area_t  *hAfRegions =
            (cam_area_t *)POINTER_OF(CAM_INTF_META_AF_ROI, metadata);
        int32_t afRegions[5];
        convertToRegions(hAfRegions->rect, afRegions, hAfRegions->weight);
        camMetadata.update(ANDROID_CONTROL_AF_REGIONS, afRegions, 5);
    }

    uint8_t  *afState = (uint8_t *)POINTER_OF(CAM_INTF_META_AF_STATE, metadata);
    camMetadata.update(ANDROID_CONTROL_AF_STATE, afState, 1);

    camMetadata.update(ANDROID_CONTROL_AF_TRIGGER, &mAfTrigger.trigger, 1);

    int32_t  *afTriggerId =
        (int32_t *)POINTER_OF(CAM_INTF_META_AF_TRIGGER_ID, metadata);
    camMetadata.update(ANDROID_CONTROL_AF_TRIGGER_ID, afTriggerId, 1);

    //uint8_t  *whiteBalance =
    //    (uint8_t *)POINTER_OF(CAM_INTF_PARM_WHITE_BALANCE, metadata);
    camMetadata.update(ANDROID_CONTROL_AWB_MODE, &mAwbMode, 1);

    uint8_t  *whiteBalanceState =
        (uint8_t *)POINTER_OF(CAM_INTF_META_AWB_STATE, metadata);
    if (mAwbLock == 1 && *whiteBalanceState == 0) {
        *whiteBalanceState = ANDROID_CONTROL_AWB_STATE_LOCKED;
    } else if (mAwbMode != ANDROID_CONTROL_AWB_MODE_OFF &&
        *whiteBalanceState == 0) {
        *whiteBalanceState = ANDROID_CONTROL_AWB_STATE_CONVERGED;
    }
    camMetadata.update(ANDROID_CONTROL_AWB_STATE, whiteBalanceState, 1);

    uint8_t  *awb_lock =
      (uint8_t *)POINTER_OF(CAM_INTF_PARM_AWB_LOCK, metadata);
    camMetadata.update(ANDROID_CONTROL_AWB_LOCK, awb_lock, 1);

    uint8_t *precaptureTrigger =
        (uint8_t *)POINTER_OF(CAM_INTF_META_AEC_PRECAPTURE_TRIGGER, metadata);
    camMetadata.update(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER,
         precaptureTrigger, 1);

    //uint8_t  *mode = (uint8_t *)POINTER_OF(CAM_INTF_META_MODE, metadata);
    camMetadata.update(ANDROID_CONTROL_MODE, &mControlMode, 1);

    //uint8_t  *edgeMode = (uint8_t *)POINTER_OF(CAM_INTF_META_EDGE_MODE, metadata);
    camMetadata.update(ANDROID_EDGE_MODE, &mEdgeMode, 1);

    uint8_t  *flashPower =
        (uint8_t *)POINTER_OF(CAM_INTF_META_FLASH_POWER, metadata);
    camMetadata.update(ANDROID_FLASH_FIRING_POWER, flashPower, 1);

    int64_t  *flashFiringTime =
        (int64_t *)POINTER_OF(CAM_INTF_META_FLASH_FIRING_TIME, metadata);
    camMetadata.update(ANDROID_FLASH_FIRING_TIME, flashFiringTime, 1);

    /*int32_t  *ledMode =
      (int32_t *)POINTER_OF(CAM_INTF_PARM_LED_MODE, metadata);
      camMetadata.update(ANDROID_FLASH_FIRING_TIME, ledMode, 1);*/

    uint8_t  *flashState =
        (uint8_t *)POINTER_OF(CAM_INTF_META_FLASH_STATE, metadata);
    camMetadata.update(ANDROID_FLASH_STATE, flashState, 1);

    static const uint8_t flashMode = ANDROID_FLASH_MODE_OFF;
    camMetadata.update(ANDROID_FLASH_MODE, &flashMode, 1);

    uint8_t  *hotPixelMode =
        (uint8_t *)POINTER_OF(CAM_INTF_META_HOTPIXEL_MODE, metadata);
    camMetadata.update(ANDROID_HOT_PIXEL_MODE, hotPixelMode, 1);

    float  *lensAperture =
        (float *)POINTER_OF(CAM_INTF_META_LENS_APERTURE, metadata);
    camMetadata.update(ANDROID_LENS_APERTURE , lensAperture, 1);

    float  *filterDensity =
        (float *)POINTER_OF(CAM_INTF_META_LENS_FILTERDENSITY, metadata);
    camMetadata.update(ANDROID_LENS_FILTER_DENSITY , filterDensity, 1);

    float  *focalLength =
        (float *)POINTER_OF(CAM_INTF_META_LENS_FOCAL_LENGTH, metadata);
    camMetadata.update(ANDROID_LENS_FOCAL_LENGTH, focalLength, 1);

    float  *focusDistance =
        (float *)POINTER_OF(CAM_INTF_META_LENS_FOCUS_DISTANCE, metadata);
    camMetadata.update(ANDROID_LENS_FOCUS_DISTANCE , focusDistance, 1);

    float  *focusRange =
        (float *)POINTER_OF(CAM_INTF_META_LENS_FOCUS_RANGE, metadata);
    camMetadata.update(ANDROID_LENS_FOCUS_RANGE , focusRange, 1);

    uint8_t  *opticalStab =
        (uint8_t *)POINTER_OF(CAM_INTF_META_LENS_OPT_STAB_MODE, metadata);
    camMetadata.update(ANDROID_LENS_OPTICAL_STABILIZATION_MODE, opticalStab, 1);

    /*int32_t  *focusState =
      (int32_t *)POINTER_OF(CAM_INTF_META_LENS_FOCUS_STATE, metadata);
      camMetadata.update(ANDROID_LENS_STATE , focusState, 1); //check */

    //uint8_t  *noiseRedMode =
    //    (uint8_t *)POINTER_OF(CAM_INTF_META_NOISE_REDUCTION_MODE, metadata);
    camMetadata.update(ANDROID_NOISE_REDUCTION_MODE, &mNoiseReductionMode, 1);

    /*CAM_INTF_META_SCALER_CROP_REGION - check size*/

    cam_crop_region_t  *hScalerCropRegion =(cam_crop_region_t *)
        POINTER_OF(CAM_INTF_META_SCALER_CROP_REGION, metadata);
    int32_t scalerCropRegion[4];
    scalerCropRegion[0] = hScalerCropRegion->left;
    scalerCropRegion[1] = hScalerCropRegion->top;
    scalerCropRegion[2] = hScalerCropRegion->width;
    scalerCropRegion[3] = hScalerCropRegion->height;
    camMetadata.update(ANDROID_SCALER_CROP_REGION, scalerCropRegion, 4);

    int64_t  *sensorExpTime =
        (int64_t *)POINTER_OF(CAM_INTF_META_SENSOR_EXPOSURE_TIME, metadata);
    mMetadataResponse.exposure_time = *sensorExpTime;
    camMetadata.update(ANDROID_SENSOR_EXPOSURE_TIME , sensorExpTime, 1);

    //int64_t  *sensorFameDuration =
    //    (int64_t *)POINTER_OF(CAM_INTF_META_SENSOR_FRAME_DURATION, metadata);
    camMetadata.update(ANDROID_SENSOR_FRAME_DURATION, &mSensorFrameDuration, 1);

    camMetadata.update(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW, &mSensorFrameDuration, 1);

    int32_t  *sensorSensitivity =
        (int32_t *)POINTER_OF(CAM_INTF_META_SENSOR_SENSITIVITY, metadata);
    mMetadataResponse.iso_speed = *sensorSensitivity;
    camMetadata.update(ANDROID_SENSOR_SENSITIVITY, sensorSensitivity, 1);

    //uint8_t *sceneMode =
    //    (uint8_t *)POINTER_OF(CAM_INTF_PARM_BESTSHOT_MODE, metadata);
    //uint8_t fwkSceneMode =
    //    (uint8_t)lookupFwkName(SCENE_MODES_MAP,
    //    sizeof(SCENE_MODES_MAP)/
    //    sizeof(SCENE_MODES_MAP[0]), *sceneMode);
    camMetadata.update(ANDROID_CONTROL_SCENE_MODE, &mSceneMode, 1);

    camMetadata.update(ANDROID_TONEMAP_MODE, &mTonemapMode, 1);

    cam_tonemap_curve_t *tonemapCurveRed =
       (cam_tonemap_curve_t *)POINTER_OF(CAM_INTF_META_TONEMAP_CURVE_RED, metadata);
    camMetadata.update(ANDROID_TONEMAP_CURVE_RED,
        (float*) tonemapCurveRed->tonemap_points,
        64 * 2);

    cam_tonemap_curve_t *tonemapCurveGreen =
       (cam_tonemap_curve_t *)POINTER_OF(CAM_INTF_META_TONEMAP_CURVE_GREEN, metadata);
    camMetadata.update(ANDROID_TONEMAP_CURVE_GREEN,
        (float*) tonemapCurveGreen->tonemap_points,
        64 * 2);

    cam_tonemap_curve_t *tonemapCurveBlue =
       (cam_tonemap_curve_t *)POINTER_OF(CAM_INTF_META_TONEMAP_CURVE_BLUE, metadata);
    camMetadata.update(ANDROID_TONEMAP_CURVE_BLUE,
        (float*) tonemapCurveBlue->tonemap_points,
        64 * 2);

    uint8_t  *shadingMode =
        (uint8_t *)POINTER_OF(CAM_INTF_META_SHADING_MODE, metadata);
    camMetadata.update(ANDROID_SHADING_MODE, shadingMode, 1);

    uint8_t  *shadingMapMode =
       (uint8_t *)POINTER_OF(CAM_INTF_META_LENS_SHADING_MAP_MODE, metadata);
    camMetadata.update(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, shadingMapMode, 1);

    uint8_t  *faceDetectMode =
        (uint8_t *)POINTER_OF(CAM_INTF_META_STATS_FACEDETECT_MODE, metadata);
    camMetadata.update(ANDROID_STATISTICS_FACE_DETECT_MODE, faceDetectMode, 1);

    uint8_t  *histogramMode =
        (uint8_t *)POINTER_OF(CAM_INTF_META_STATS_HISTOGRAM_MODE, metadata);
    camMetadata.update(ANDROID_STATISTICS_HISTOGRAM_MODE, histogramMode, 1);

    uint8_t  *sharpnessMapMode =
        (uint8_t *)POINTER_OF(CAM_INTF_META_STATS_SHARPNESS_MAP_MODE, metadata);
    camMetadata.update(ANDROID_STATISTICS_SHARPNESS_MAP_MODE,
            sharpnessMapMode, 1);

    /*CAM_INTF_META_STATS_SHARPNESS_MAP - check size*/
    cam_sharpness_map_t  *sharpnessMap = (cam_sharpness_map_t *)
        POINTER_OF(CAM_INTF_META_STATS_SHARPNESS_MAP, metadata);
    camMetadata.update(ANDROID_STATISTICS_SHARPNESS_MAP,
            (int32_t*)sharpnessMap->sharpness,
            CAM_MAX_MAP_WIDTH*CAM_MAX_MAP_HEIGHT);

    cam_lens_shading_map_t *lensShadingMap = (cam_lens_shading_map_t *)
        POINTER_OF(CAM_INTF_META_LENS_SHADING_MAP, metadata);
    int map_height = gCamCapability[mCameraId]->lens_shading_map_size.height;
    int map_width  = gCamCapability[mCameraId]->lens_shading_map_size.width;
    camMetadata.update(ANDROID_STATISTICS_LENS_SHADING_MAP,
                       (float*)lensShadingMap->lens_shading,
                       4*map_width*map_height);

    //cam_color_correct_gains_t *colorCorrectionGains = (cam_color_correct_gains_t*)
    //    POINTER_OF(CAM_INTF_META_COLOR_CORRECT_GAINS, metadata);
    camMetadata.update(ANDROID_COLOR_CORRECTION_GAINS, mColorCorrectGains.gains, 4);

    cam_color_correct_matrix_t *colorCorrectionMatrix = (cam_color_correct_matrix_t*)
        POINTER_OF(CAM_INTF_META_COLOR_CORRECT_TRANSFORM, metadata);
    camMetadata.update(ANDROID_COLOR_CORRECTION_TRANSFORM,
                       (camera_metadata_rational_t*)colorCorrectionMatrix->transform_matrix, 3*3);

    cam_color_correct_gains_t *predColorCorrectionGains = (cam_color_correct_gains_t*)
        POINTER_OF(CAM_INTF_META_PRED_COLOR_CORRECT_GAINS, metadata);
    camMetadata.update(ANDROID_STATISTICS_PREDICTED_COLOR_GAINS,
                       predColorCorrectionGains->gains, 4);

    cam_color_correct_matrix_t *predColorCorrectionMatrix = (cam_color_correct_matrix_t*)
        POINTER_OF(CAM_INTF_META_PRED_COLOR_CORRECT_TRANSFORM, metadata);
    camMetadata.update(ANDROID_STATISTICS_PREDICTED_COLOR_TRANSFORM,
                       (camera_metadata_rational_t*)predColorCorrectionMatrix->transform_matrix, 3*3);

    uint8_t *blackLevelLock = (uint8_t*)
        POINTER_OF(CAM_INTF_META_BLACK_LEVEL_LOCK, metadata);
    camMetadata.update(ANDROID_BLACK_LEVEL_LOCK, blackLevelLock, 1);

    uint8_t *hal_ab_mode =
      (uint8_t *)POINTER_OF(CAM_INTF_PARM_ANTIBANDING, metadata);
    uint8_t fwk_ab_mode = (uint8_t)lookupFwkName(ANTIBANDING_MODES_MAP,
             sizeof(ANTIBANDING_MODES_MAP)/sizeof(ANTIBANDING_MODES_MAP[0]),
             *hal_ab_mode);
    camMetadata.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE,
        &fwk_ab_mode, 1);

    uint8_t *captureIntent = (uint8_t*)
      POINTER_OF(CAM_INTF_META_CAPTURE_INTENT, metadata);
    camMetadata.update(ANDROID_CONTROL_CAPTURE_INTENT, captureIntent, 1);

    uint8_t *sceneFlicker = (uint8_t*)
        POINTER_OF(CAM_INTF_META_SCENE_FLICKER, metadata);
    camMetadata.update(ANDROID_STATISTICS_SCENE_FLICKER, sceneFlicker, 1);

    //uint8_t *effectMode = (uint8_t*) POINTER_OF(CAM_INTF_PARM_EFFECT, metadata);
    //uint8_t fwk_effectMode = (uint8_t)lookupFwkName(EFFECT_MODES_MAP,
    //                                       sizeof(EFFECT_MODES_MAP),
    //                                       *effectMode);
    camMetadata.update(ANDROID_CONTROL_EFFECT_MODE, &mEffectMode, 1);

    /* Constant metadata values to be update*/
    static const uint8_t vs_mode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF;
    camMetadata.update(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &vs_mode, 1);

    static const uint8_t hotPixelMapMode = ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF;
    camMetadata.update(ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE, &hotPixelMapMode, 1);

    int32_t hotPixelMap[2];
    camMetadata.update(ANDROID_STATISTICS_HOT_PIXEL_MAP, &hotPixelMap[0], 0);

    static const uint8_t cac = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF;
    camMetadata.update(ANDROID_COLOR_CORRECTION_ABERRATION_MODE, &cac, 1);

    static const int32_t testPatternMode = ANDROID_SENSOR_TEST_PATTERN_MODE_OFF;
    camMetadata.update(ANDROID_SENSOR_TEST_PATTERN_MODE, &testPatternMode, 1);

#if 0
    uint8_t fwk_aeMode;
    int32_t *redeye = (int32_t*)
            POINTER_OF(CAM_INTF_PARM_REDEYE_REDUCTION, metadata);
    uint8_t *aeMode = (uint8_t*) POINTER_OF(CAM_INTF_META_AEC_MODE, metadata);
    if (redeye != NULL && *redeye == 1) {
        fwk_aeMode = ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH_REDEYE;
        camMetadata.update(ANDROID_CONTROL_AE_MODE, &fwk_aeMode, 1);
    } else if (aeMode != NULL && *aeMode == CAM_AE_MODE_ON) {
        fwk_aeMode = ANDROID_CONTROL_AE_MODE_ON;
        camMetadata.update(ANDROID_CONTROL_AE_MODE, &fwk_aeMode, 1);
    } else if (aeMode != NULL && *aeMode == CAM_AE_MODE_OFF) {
        fwk_aeMode = ANDROID_CONTROL_AE_MODE_OFF;
        camMetadata.update(ANDROID_CONTROL_AE_MODE, &fwk_aeMode, 1);
    } else {
        ALOGE("%s: Not enough info to deduce ANDROID_CONTROL_AE_MODE redeye: %p, aeMode: %p",
            __func__, redeye, aeMode);
    }
#endif
    camMetadata.update(ANDROID_CONTROL_AE_MODE, &mAeMode, 1);

    resultMetadata = camMetadata.release();
    return resultMetadata;
}

/*===========================================================================
 * FUNCTION   : convertToRegions
 *
 * DESCRIPTION: helper method to convert from cam_rect_t into int32_t array
 *
 * PARAMETERS :
 *   @rect   : cam_rect_t struct to convert
 *   @region : int32_t destination array
 *   @weight : if we are converting from cam_area_t, weight is valid
 *             else weight = -1
 *
 *==========================================================================*/
void QCamera3HardwareInterface::convertToRegions(cam_rect_t rect, int32_t* region, int weight){
    region[0] = rect.left;
    region[1] = rect.top;
    region[2] = rect.left + rect.width;
    region[3] = rect.top + rect.height;
    if (weight > -1) {
        region[4] = weight;
    }
}

/*===========================================================================
 * FUNCTION   : convertFromRegions
 *
 * DESCRIPTION: helper method to convert from array to cam_rect_t
 *
 * PARAMETERS :
 *   @rect   : cam_rect_t struct to convert
 *   @region : int32_t destination array
 *   @weight : if we are converting from cam_area_t, weight is valid
 *             else weight = -1
 *
 *==========================================================================*/
void QCamera3HardwareInterface::convertFromRegions(cam_area_t* roi,
                                                   const camera_metadata_t *settings,
                                                   uint32_t tag){
    CameraMetadata frame_settings;
    frame_settings = settings;
    int32_t x_min = frame_settings.find(tag).data.i32[0];
    int32_t y_min = frame_settings.find(tag).data.i32[1];
    int32_t x_max = frame_settings.find(tag).data.i32[2];
    int32_t y_max = frame_settings.find(tag).data.i32[3];
    roi->weight = frame_settings.find(tag).data.i32[4];
    roi->rect.left = x_min;
    roi->rect.top = y_min;
    roi->rect.width = x_max - x_min;
    roi->rect.height = y_max - y_min;
}

/*===========================================================================
 * FUNCTION   : resetIfNeededROI
 *
 * DESCRIPTION: helper method to reset the roi if it is greater than scaler
 *              crop region
 *
 * PARAMETERS :
 *   @roi       : cam_area_t struct to resize
 *   @scalerCropRegion : cam_crop_region_t region to compare against
 *
 *
 *==========================================================================*/
bool QCamera3HardwareInterface::resetIfNeededROI(cam_area_t* roi,
                                                 const cam_crop_region_t* scalerCropRegion)
{
    int32_t roi_x_max = roi->rect.width + roi->rect.left;
    int32_t roi_y_max = roi->rect.height + roi->rect.top;
    int32_t crop_x_max = scalerCropRegion->width + scalerCropRegion->top;
    int32_t crop_y_max = scalerCropRegion->height + scalerCropRegion->left;
    if ((roi_x_max < scalerCropRegion->left) ||
        (roi_y_max < scalerCropRegion->top)  ||
        (roi->rect.left > crop_x_max) ||
        (roi->rect.top > crop_y_max)){
        return false;
    }
    if (roi->rect.left < scalerCropRegion->left) {
        roi->rect.left = scalerCropRegion->left;
    }
    if (roi->rect.top < scalerCropRegion->top) {
        roi->rect.top = scalerCropRegion->top;
    }
    if (roi_x_max > crop_x_max) {
        roi_x_max = crop_x_max;
    }
    if (roi_y_max > crop_y_max) {
        roi_y_max = crop_y_max;
    }
    roi->rect.width = roi_x_max - roi->rect.left;
    roi->rect.height = roi_y_max - roi->rect.top;
    return true;
}

/*===========================================================================
 * FUNCTION   : convertLandmarks
 *
 * DESCRIPTION: helper method to extract the landmarks from face detection info
 *
 * PARAMETERS :
 *   @face   : cam_rect_t struct to convert
 *   @landmarks : int32_t destination array
 *
 *
 *==========================================================================*/
void QCamera3HardwareInterface::convertLandmarks(cam_face_detection_info_t face, int32_t* landmarks)
{
    landmarks[0] = face.left_eye_center.x;
    landmarks[1] = face.left_eye_center.y;
    landmarks[2] = face.right_eye_center.y;
    landmarks[3] = face.right_eye_center.y;
    landmarks[4] = face.mouth_center.x;
    landmarks[5] = face.mouth_center.y;
}

#define DATA_PTR(MEM_OBJ,INDEX) MEM_OBJ->getPtr( INDEX )
/*===========================================================================
 * FUNCTION   : initCapabilities
 *
 * DESCRIPTION: initialize camera capabilities in static data struct
 *
 * PARAMETERS :
 *   @cameraId  : camera Id
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera3HardwareInterface::initCapabilities(int cameraId)
{
    int rc = 0;
    mm_camera_vtbl_t *cameraHandle = NULL;
    QCamera3HeapMemory *capabilityHeap = NULL;

    cameraHandle = camera_open(cameraId);
    if (!cameraHandle) {
        ALOGE("%s: camera_open failed", __func__);
        rc = -1;
        goto open_failed;
    }

    capabilityHeap = new QCamera3HeapMemory();
    if (capabilityHeap == NULL) {
        ALOGE("%s: creation of capabilityHeap failed", __func__);
        goto heap_creation_failed;
    }
    /* Allocate memory for capability buffer */
    rc = capabilityHeap->allocate(1, sizeof(cam_capability_t), false);
    if(rc != OK) {
        ALOGE("%s: No memory for cappability", __func__);
        goto allocate_failed;
    }

    /* Map memory for capability buffer */
    memset(DATA_PTR(capabilityHeap,0), 0, sizeof(cam_capability_t));
    rc = cameraHandle->ops->map_buf(cameraHandle->camera_handle,
                                CAM_MAPPING_BUF_TYPE_CAPABILITY,
                                capabilityHeap->getFd(0),
                                sizeof(cam_capability_t));
    if(rc < 0) {
        ALOGE("%s: failed to map capability buffer", __func__);
        goto map_failed;
    }

    /* Query Capability */
    rc = cameraHandle->ops->query_capability(cameraHandle->camera_handle);
    if(rc < 0) {
        ALOGE("%s: failed to query capability",__func__);
        goto query_failed;
    }
    gCamCapability[cameraId] = (cam_capability_t *)malloc(sizeof(cam_capability_t));
    if (!gCamCapability[cameraId]) {
        ALOGE("%s: out of memory", __func__);
        goto query_failed;
    }
    memcpy(gCamCapability[cameraId], DATA_PTR(capabilityHeap,0),
                                        sizeof(cam_capability_t));
    rc = 0;

query_failed:
    cameraHandle->ops->unmap_buf(cameraHandle->camera_handle,
                            CAM_MAPPING_BUF_TYPE_CAPABILITY);
map_failed:
    capabilityHeap->deallocate();
allocate_failed:
    delete capabilityHeap;
heap_creation_failed:
    cameraHandle->ops->close_camera(cameraHandle->camera_handle);
    cameraHandle = NULL;
open_failed:
    return rc;
}

/*===========================================================================
 * FUNCTION   : initParameters
 *
 * DESCRIPTION: initialize camera parameters
 *
 * PARAMETERS :
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera3HardwareInterface::initParameters()
{
    int rc = 0;

    //Allocate Set Param Buffer
    mParamHeap = new QCamera3HeapMemory();
    rc = mParamHeap->allocate(1, sizeof(parm_buffer_t), false);
    if(rc != OK) {
        rc = NO_MEMORY;
        ALOGE("Failed to allocate SETPARM Heap memory");
        delete mParamHeap;
        mParamHeap = NULL;
        return rc;
    }

    //Map memory for parameters buffer
    rc = mCameraHandle->ops->map_buf(mCameraHandle->camera_handle,
            CAM_MAPPING_BUF_TYPE_PARM_BUF,
            mParamHeap->getFd(0),
            sizeof(parm_buffer_t));
    if(rc < 0) {
        ALOGE("%s:failed to map SETPARM buffer",__func__);
        rc = FAILED_TRANSACTION;
        mParamHeap->deallocate();
        delete mParamHeap;
        mParamHeap = NULL;
        return rc;
    }

    mParameters = (parm_buffer_t*) DATA_PTR(mParamHeap,0);
    return rc;
}

/*===========================================================================
 * FUNCTION   : deinitParameters
 *
 * DESCRIPTION: de-initialize camera parameters
 *
 * PARAMETERS :
 *
 * RETURN     : NONE
 *==========================================================================*/
void QCamera3HardwareInterface::deinitParameters()
{
    mCameraHandle->ops->unmap_buf(mCameraHandle->camera_handle,
            CAM_MAPPING_BUF_TYPE_PARM_BUF);

    mParamHeap->deallocate();
    delete mParamHeap;
    mParamHeap = NULL;

    mParameters = NULL;
}

/*===========================================================================
 * FUNCTION   : calcMaxJpegSize
 *
 * DESCRIPTION: Calculates maximum jpeg size supported by the cameraId
 *
 * PARAMETERS :
 *
 * RETURN     : max_jpeg_size
 *==========================================================================*/
int QCamera3HardwareInterface::calcMaxJpegSize()
{
    int32_t max_jpeg_size = 0;
    int temp_width, temp_height;
    for (int i = 0; i < gCamCapability[mCameraId]->picture_sizes_tbl_cnt; i++) {
        temp_width = gCamCapability[mCameraId]->picture_sizes_tbl[i].width;
        temp_height = gCamCapability[mCameraId]->picture_sizes_tbl[i].height;
        if (temp_width * temp_height > max_jpeg_size ) {
            max_jpeg_size = temp_width * temp_height;
        }
    }
    max_jpeg_size = max_jpeg_size * 3/2 + sizeof(camera3_jpeg_blob_t);
    return max_jpeg_size;
}

/*===========================================================================
 * FUNCTION   : initStaticMetadata
 *
 * DESCRIPTION: initialize the static metadata
 *
 * PARAMETERS :
 *   @cameraId  : camera Id
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              non-zero failure code
 *==========================================================================*/
int QCamera3HardwareInterface::initStaticMetadata(int cameraId)
{
    int rc = 0;
    CameraMetadata staticInfo;

    /* android.info: hardware level */
    uint8_t supportedHardwareLevel = ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED;
    staticInfo.update(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
        &supportedHardwareLevel, 1);

    int facingBack = gCamCapability[cameraId]->position == CAM_POSITION_BACK;
    /*HAL 3 only*/
    /*staticInfo.update(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
                    &gCamCapability[cameraId]->min_focus_distance, 1); */

    /*hard coded for now but this should come from sensor*/
    float min_focus_distance;
    if(facingBack){
        min_focus_distance = 10;
    } else {
        min_focus_distance = 0;
    }
    staticInfo.update(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
                    &min_focus_distance, 1);

    staticInfo.update(ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE,
                    &gCamCapability[cameraId]->hyper_focal_distance, 1);

    /*should be using focal lengths but sensor doesn't provide that info now*/
    staticInfo.update(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
                      &gCamCapability[cameraId]->focal_length,
                      1);

    staticInfo.update(ANDROID_LENS_INFO_AVAILABLE_APERTURES,
                      gCamCapability[cameraId]->apertures,
                      gCamCapability[cameraId]->apertures_count);

    staticInfo.update(ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES,
                gCamCapability[cameraId]->filter_densities,
                gCamCapability[cameraId]->filter_densities_count);


    staticInfo.update(ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
                      (uint8_t*)gCamCapability[cameraId]->optical_stab_modes,
                      gCamCapability[cameraId]->optical_stab_modes_count);

    int32_t lens_shading_map_size[] = {gCamCapability[cameraId]->lens_shading_map_size.width,
                                       gCamCapability[cameraId]->lens_shading_map_size.height};
    staticInfo.update(ANDROID_LENS_INFO_SHADING_MAP_SIZE,
                      lens_shading_map_size,
                      sizeof(lens_shading_map_size)/sizeof(int32_t));

    staticInfo.update(ANDROID_SENSOR_INFO_PHYSICAL_SIZE,
            gCamCapability[cameraId]->sensor_physical_size, 2);

    staticInfo.update(ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE,
            gCamCapability[cameraId]->exposure_time_range, 2);

    int64_t max_frame_duration = gCamCapability[cameraId]->max_frame_duration;
    if (max_frame_duration == 0) {
        int min_fps = INT_MAX;
        for (int i = 0; i < gCamCapability[cameraId]->fps_ranges_tbl_cnt; ++i) {
            if (min_fps > gCamCapability[cameraId]->fps_ranges_tbl[i].min_fps)
                min_fps = gCamCapability[cameraId]->fps_ranges_tbl[i].min_fps;
        }
        max_frame_duration = NSEC_PER_SEC / min_fps;
    }
    staticInfo.update(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
            &max_frame_duration, 1);

    camera_metadata_rational baseGainFactor = {
            gCamCapability[cameraId]->base_gain_factor.numerator,
            gCamCapability[cameraId]->base_gain_factor.denominator};
    staticInfo.update(ANDROID_SENSOR_BASE_GAIN_FACTOR,
            &baseGainFactor, 1);

    staticInfo.update(ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT,
                     (uint8_t*)&gCamCapability[cameraId]->color_arrangement, 1);

    int32_t pixel_array_size[] = {gCamCapability[cameraId]->active_array_size.width,
                                  gCamCapability[cameraId]->active_array_size.height};
    staticInfo.update(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
                      pixel_array_size, 2);

    int32_t active_array_size[] = {0, 0,
                                                gCamCapability[cameraId]->active_array_size.width,
                                                gCamCapability[cameraId]->active_array_size.height};
    staticInfo.update(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
                      active_array_size, 4);

    staticInfo.update(ANDROID_SENSOR_INFO_WHITE_LEVEL,
            &gCamCapability[cameraId]->white_level, 1);

    staticInfo.update(ANDROID_SENSOR_BLACK_LEVEL_PATTERN,
            gCamCapability[cameraId]->black_level_pattern, 4);

    staticInfo.update(ANDROID_FLASH_INFO_CHARGE_DURATION,
                      &gCamCapability[cameraId]->flash_charge_duration, 1);

    if (gCamCapability[cameraId]->max_tone_map_curve_points == 0) {
        gCamCapability[cameraId]->max_tone_map_curve_points = 64;
    }
    staticInfo.update(ANDROID_TONEMAP_MAX_CURVE_POINTS,
                      &gCamCapability[cameraId]->max_tone_map_curve_points, 1);

    /*staticInfo.update(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT,
                      (int*)&gCamCapability[cameraId]->max_face_detection_count, 1);*/
    /*hardcode 0 for now*/
    int32_t max_face_count = 0;
    staticInfo.update(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT,
                      &max_face_count, 1);

    staticInfo.update(ANDROID_STATISTICS_INFO_HISTOGRAM_BUCKET_COUNT,
                      &gCamCapability[cameraId]->histogram_size, 1);

    staticInfo.update(ANDROID_STATISTICS_INFO_MAX_HISTOGRAM_COUNT,
            &gCamCapability[cameraId]->max_histogram_count, 1);

    int32_t sharpness_map_size[] = {gCamCapability[cameraId]->sharpness_map_size.width,
                                    gCamCapability[cameraId]->sharpness_map_size.height};

    staticInfo.update(ANDROID_STATISTICS_INFO_SHARPNESS_MAP_SIZE,
            sharpness_map_size, sizeof(sharpness_map_size)/sizeof(int32_t));

    staticInfo.update(ANDROID_STATISTICS_INFO_MAX_SHARPNESS_MAP_VALUE,
            &gCamCapability[cameraId]->max_sharpness_map_value, 1);

    int32_t scalar_formats[] = {HAL_PIXEL_FORMAT_YCbCr_420_888,
                                HAL_PIXEL_FORMAT_BLOB,
                           HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED};
    int scalar_formats_count = sizeof(scalar_formats)/sizeof(int32_t);
    staticInfo.update(ANDROID_SCALER_AVAILABLE_FORMATS,
                      scalar_formats,
                      scalar_formats_count);

    int32_t available_processed_sizes[CAM_FORMAT_MAX * 2];
    makeTable(gCamCapability[cameraId]->picture_sizes_tbl,
              gCamCapability[cameraId]->picture_sizes_tbl_cnt,
              available_processed_sizes);
    staticInfo.update(ANDROID_SCALER_AVAILABLE_PROCESSED_SIZES,
                available_processed_sizes,
                (gCamCapability[cameraId]->picture_sizes_tbl_cnt) * 2);

    int32_t available_fps_ranges[MAX_SIZES_CNT * 2];
    makeFPSTable(gCamCapability[cameraId]->fps_ranges_tbl,
                 gCamCapability[cameraId]->fps_ranges_tbl_cnt,
                 available_fps_ranges);
    staticInfo.update(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
            available_fps_ranges, (gCamCapability[cameraId]->fps_ranges_tbl_cnt*2) );

    camera_metadata_rational exposureCompensationStep = {
            gCamCapability[cameraId]->exp_compensation_step.numerator,
            gCamCapability[cameraId]->exp_compensation_step.denominator};
    staticInfo.update(ANDROID_CONTROL_AE_COMPENSATION_STEP,
                      &exposureCompensationStep, 1);

    /*TO DO*/
    uint8_t availableVstabModes[] = {ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF};
    staticInfo.update(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
                      availableVstabModes, sizeof(availableVstabModes));

    /*HAL 1 and HAL 3 common*/
    float maxZoom = 4;
    staticInfo.update(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
            &maxZoom, 1);

    uint8_t croppingType = ANDROID_SCALER_CROPPING_TYPE_CENTER_ONLY;
    staticInfo.update(ANDROID_SCALER_CROPPING_TYPE, &croppingType, 1);

    int32_t max3aRegions[3] = {/*AE*/1,/*AWB*/ 0,/*AF*/ 1};
    if (gCamCapability[cameraId]->supported_focus_modes_cnt == 1)
        max3aRegions[2] = 0; /* AF not supported */
    staticInfo.update(ANDROID_CONTROL_MAX_REGIONS,
            max3aRegions, 3);

    uint8_t availableFaceDetectModes[] = {
            ANDROID_STATISTICS_FACE_DETECT_MODE_OFF };
    staticInfo.update(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
                      availableFaceDetectModes,
                      sizeof(availableFaceDetectModes));

    int32_t exposureCompensationRange[] = {gCamCapability[cameraId]->exposure_compensation_min,
                                           gCamCapability[cameraId]->exposure_compensation_max};
    staticInfo.update(ANDROID_CONTROL_AE_COMPENSATION_RANGE,
            exposureCompensationRange,
            sizeof(exposureCompensationRange)/sizeof(int32_t));

    uint8_t lensFacing = (facingBack) ?
            ANDROID_LENS_FACING_BACK : ANDROID_LENS_FACING_FRONT;
    staticInfo.update(ANDROID_LENS_FACING, &lensFacing, 1);

    staticInfo.update(ANDROID_SCALER_AVAILABLE_JPEG_SIZES,
                available_processed_sizes,
                (gCamCapability[cameraId]->picture_sizes_tbl_cnt * 2));

    staticInfo.update(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
                      available_thumbnail_sizes,
                      sizeof(available_thumbnail_sizes)/sizeof(int32_t));

    /*all sizes will be clubbed into this tag*/
    int32_t available_stream_configs_size = gCamCapability[cameraId]->picture_sizes_tbl_cnt *
                                    sizeof(scalar_formats)/sizeof(int32_t) * 4;
    int32_t available_stream_configs[available_stream_configs_size];
    int idx = 0;
    for (int j = 0; j < scalar_formats_count; j++) {
        for (int i = 0; i < gCamCapability[cameraId]->picture_sizes_tbl_cnt; i++) {
           available_stream_configs[idx] = scalar_formats[j];
           available_stream_configs[idx+1] = gCamCapability[cameraId]->picture_sizes_tbl[i].width;
           available_stream_configs[idx+2] = gCamCapability[cameraId]->picture_sizes_tbl[i].height;
           available_stream_configs[idx+3] = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;
           idx+=4;
        }
    }

    staticInfo.update(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                      available_stream_configs,
                      available_stream_configs_size);



    int32_t max_jpeg_size = 0;
    int temp_width, temp_height;
    for (int i = 0; i < gCamCapability[cameraId]->picture_sizes_tbl_cnt; i++) {
        temp_width = gCamCapability[cameraId]->picture_sizes_tbl[i].width;
        temp_height = gCamCapability[cameraId]->picture_sizes_tbl[i].height;
        if (temp_width * temp_height > max_jpeg_size ) {
            max_jpeg_size = temp_width * temp_height;
        }
    }
    max_jpeg_size = max_jpeg_size * 3/2 + sizeof(camera3_jpeg_blob_t);
    staticInfo.update(ANDROID_JPEG_MAX_SIZE,
                      &max_jpeg_size, 1);

    uint8_t avail_effects[CAM_EFFECT_MODE_MAX];
    int32_t size = 0;
    for (int i = 0; i < gCamCapability[cameraId]->supported_effects_cnt; i++) {
        int val = lookupFwkName(EFFECT_MODES_MAP,
                                   sizeof(EFFECT_MODES_MAP)/sizeof(EFFECT_MODES_MAP[0]),
                                   gCamCapability[cameraId]->supported_effects[i]);
        if (val != NAME_NOT_FOUND) {
            avail_effects[size] = (uint8_t)val;
            size++;
        }
    }
    staticInfo.update(ANDROID_CONTROL_AVAILABLE_EFFECTS,
                      avail_effects,
                      size);

    uint8_t avail_scene_modes[CAM_SCENE_MODE_MAX];
    uint8_t supported_indexes[CAM_SCENE_MODE_MAX];
    int32_t supported_scene_modes_cnt = 0;
    for (int i = 0; i < gCamCapability[cameraId]->supported_scene_modes_cnt; i++) {
        int val = lookupFwkName(SCENE_MODES_MAP,
                                sizeof(SCENE_MODES_MAP)/sizeof(SCENE_MODES_MAP[0]),
                                gCamCapability[cameraId]->supported_scene_modes[i]);
        if (val != NAME_NOT_FOUND) {
            avail_scene_modes[supported_scene_modes_cnt] = (uint8_t)val;
            supported_indexes[supported_scene_modes_cnt] = i;
            supported_scene_modes_cnt++;
        }
    }

    staticInfo.update(ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
                      avail_scene_modes,
                      supported_scene_modes_cnt);

    uint8_t scene_mode_overrides[CAM_SCENE_MODE_MAX * 3];
    makeOverridesList(gCamCapability[cameraId]->scene_mode_overrides,
                      supported_scene_modes_cnt,
                      scene_mode_overrides,
                      supported_indexes,
                      cameraId);
    staticInfo.update(ANDROID_CONTROL_SCENE_MODE_OVERRIDES,
                      scene_mode_overrides,
                      supported_scene_modes_cnt*3);

    uint8_t available_control_modes[] = {ANDROID_CONTROL_MODE_OFF,
                                         ANDROID_CONTROL_MODE_AUTO,
                                         ANDROID_CONTROL_MODE_USE_SCENE_MODE};
    staticInfo.update(ANDROID_CONTROL_AVAILABLE_MODES,
            available_control_modes,
            3);

    uint8_t avail_antibanding_modes[CAM_ANTIBANDING_MODE_MAX];
    size = 0;
    for (int i = 0; i < gCamCapability[cameraId]->supported_antibandings_cnt; i++) {
        int val = lookupFwkName(ANTIBANDING_MODES_MAP,
                                 sizeof(ANTIBANDING_MODES_MAP)/sizeof(ANTIBANDING_MODES_MAP[0]),
                                 gCamCapability[cameraId]->supported_antibandings[i]);
        if (val != NAME_NOT_FOUND) {
            avail_antibanding_modes[size] = (uint8_t)val;
            size++;
        }

    }
    staticInfo.update(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
                      avail_antibanding_modes,
                      size);

    uint8_t avail_af_modes[CAM_FOCUS_MODE_MAX];
    size = 0;
    for (int i = 0; i < gCamCapability[cameraId]->supported_focus_modes_cnt; i++) {
        int val = lookupFwkName(FOCUS_MODES_MAP,
                                sizeof(FOCUS_MODES_MAP)/sizeof(FOCUS_MODES_MAP[0]),
                                gCamCapability[cameraId]->supported_focus_modes[i]);
        if (val != NAME_NOT_FOUND) {
            avail_af_modes[size] = (uint8_t)val;
            size++;
        }
    }
    if (gCamCapability[cameraId]->supported_focus_modes_cnt > 1) {
        avail_af_modes[size] = ANDROID_CONTROL_AF_MODE_OFF;
        size++;
    }
    staticInfo.update(ANDROID_CONTROL_AF_AVAILABLE_MODES,
                      avail_af_modes,
                      size);

    uint8_t avail_awb_modes[CAM_WB_MODE_MAX];
    size = 0;
    for (int i = 0; i < gCamCapability[cameraId]->supported_white_balances_cnt; i++) {
        int8_t val = lookupFwkName(WHITE_BALANCE_MODES_MAP,
                                    sizeof(WHITE_BALANCE_MODES_MAP)/sizeof(WHITE_BALANCE_MODES_MAP[0]),
                                    gCamCapability[cameraId]->supported_white_balances[i]);
        if (val != NAME_NOT_FOUND) {
            avail_awb_modes[size] = (uint8_t)val;
            size++;
        }
    }
    staticInfo.update(ANDROID_CONTROL_AWB_AVAILABLE_MODES,
                      avail_awb_modes,
                      size);

    uint8_t awbLockAvailable = ANDROID_CONTROL_AWB_LOCK_AVAILABLE_TRUE;
    staticInfo.update(ANDROID_CONTROL_AWB_LOCK_AVAILABLE,
            &awbLockAvailable,
            1);

    uint8_t aeLockAvailable = ANDROID_CONTROL_AE_LOCK_AVAILABLE_TRUE;
    staticInfo.update(ANDROID_CONTROL_AE_LOCK_AVAILABLE,
            &aeLockAvailable,
            1);

    uint8_t available_flash_levels[CAM_FLASH_FIRING_LEVEL_MAX];
    for (int i = 0; i < gCamCapability[cameraId]->supported_flash_firing_level_cnt; i++)
      available_flash_levels[i] = gCamCapability[cameraId]->supported_firing_levels[i];

    staticInfo.update(ANDROID_FLASH_FIRING_POWER,
            available_flash_levels,
            gCamCapability[cameraId]->supported_flash_firing_level_cnt);

    uint8_t flashAvailable;
    if (gCamCapability[cameraId]->flash_available)
        flashAvailable = ANDROID_FLASH_INFO_AVAILABLE_TRUE;
    else
        flashAvailable = ANDROID_FLASH_INFO_AVAILABLE_FALSE;
    staticInfo.update(ANDROID_FLASH_INFO_AVAILABLE,
            &flashAvailable, 1);

    uint8_t avail_ae_modes[5];
    size = 0;
    for (int i = 0; i < gCamCapability[cameraId]->supported_ae_modes_cnt; i++) {
        avail_ae_modes[i] = gCamCapability[cameraId]->supported_ae_modes[i];
        size++;
    }
    if (flashAvailable) {
        avail_ae_modes[size++] = ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH;
        avail_ae_modes[size++] = ANDROID_CONTROL_AE_MODE_ON_ALWAYS_FLASH;
        avail_ae_modes[size++] = ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH_REDEYE;
    }
    staticInfo.update(ANDROID_CONTROL_AE_AVAILABLE_MODES,
                      avail_ae_modes,
                      size);

    int32_t sensitivity_range[2];
    sensitivity_range[0] = gCamCapability[cameraId]->sensitivity_range.min_sensitivity;
    sensitivity_range[1] = gCamCapability[cameraId]->sensitivity_range.max_sensitivity;
    if (sensitivity_range[0] == 0)
        sensitivity_range[0] = 100;
    if (sensitivity_range[1] == 0)
        sensitivity_range[1] = gCamCapability[cameraId]->max_analog_sensitivity;
    staticInfo.update(ANDROID_SENSOR_INFO_SENSITIVITY_RANGE,
                      sensitivity_range,
                      sizeof(sensitivity_range) / sizeof(int32_t));

    staticInfo.update(ANDROID_SENSOR_MAX_ANALOG_SENSITIVITY,
                      &gCamCapability[cameraId]->max_analog_sensitivity,
                      1);

    staticInfo.update(ANDROID_SCALER_AVAILABLE_JPEG_MIN_DURATIONS,
                      &gCamCapability[cameraId]->jpeg_min_duration[0],
                      gCamCapability[cameraId]->picture_sizes_tbl_cnt);

    int32_t sensor_orientation = (int32_t)gCamCapability[cameraId]->sensor_mount_angle;
    staticInfo.update(ANDROID_SENSOR_ORIENTATION,
                      &sensor_orientation,
                      1);

    int32_t max_output_streams[3] = {1, 3, 1};
    staticInfo.update(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS,
                      max_output_streams,
                      3);

    int32_t avail_testpattern_modes[] = { ANDROID_SENSOR_TEST_PATTERN_MODE_OFF };
    staticInfo.update(ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES,
                      avail_testpattern_modes,
                      1);

    uint8_t max_pipeline_depth = kMaxInFlight + EMPTY_PIPELINE_DELAY;;
    staticInfo.update(ANDROID_REQUEST_PIPELINE_MAX_DEPTH,
                      &max_pipeline_depth,
                      1);

    int32_t partial_result_count = 1;
    staticInfo.update(ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
                      &partial_result_count,
                       1);

    uint8_t available_capabilities[] =
        {ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE};
    staticInfo.update(ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                      available_capabilities,
                      1);

    int32_t max_input_streams = 0;
    staticInfo.update(ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS,
                      &max_input_streams,
                      1);

    int32_t io_format_map[] = {};
    staticInfo.update(ANDROID_SCALER_AVAILABLE_INPUT_OUTPUT_FORMATS_MAP,
                      io_format_map, 0);

    int32_t max_latency = ANDROID_SYNC_MAX_LATENCY_UNKNOWN;
    staticInfo.update(ANDROID_SYNC_MAX_LATENCY,
                      &max_latency,
                      1);

    uint8_t available_hot_pixel_modes[] = {ANDROID_HOT_PIXEL_MODE_FAST,
                                           ANDROID_HOT_PIXEL_MODE_HIGH_QUALITY};
    staticInfo.update(ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES,
                      available_hot_pixel_modes,
                      2);

    uint8_t available_shading_modes[] = {ANDROID_SHADING_MODE_OFF,
                                         ANDROID_SHADING_MODE_FAST,
                                         ANDROID_SHADING_MODE_HIGH_QUALITY};
    staticInfo.update(ANDROID_SHADING_AVAILABLE_MODES,
                      available_shading_modes,
                      3);

    uint8_t available_lens_shading_map_modes[] = {ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF,
                                                  ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_ON};
    staticInfo.update(ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
                      available_lens_shading_map_modes,
                      2);

    uint8_t available_edge_modes[] = {ANDROID_EDGE_MODE_OFF,
                                      ANDROID_EDGE_MODE_FAST,
                                      ANDROID_EDGE_MODE_HIGH_QUALITY};
    staticInfo.update(ANDROID_EDGE_AVAILABLE_EDGE_MODES,
                      available_edge_modes,
                      3);

    uint8_t available_noise_red_modes[] = {ANDROID_NOISE_REDUCTION_MODE_OFF,
                                           ANDROID_NOISE_REDUCTION_MODE_FAST,
                                           ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY};
    staticInfo.update(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
                      available_noise_red_modes,
                      3);

    uint8_t available_tonemap_modes[] = {ANDROID_TONEMAP_MODE_CONTRAST_CURVE,
                                         ANDROID_TONEMAP_MODE_FAST,
                                         ANDROID_TONEMAP_MODE_HIGH_QUALITY};
    staticInfo.update(ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES,
                      available_tonemap_modes,
                      3);

    uint8_t available_hot_pixel_map_modes[] = {ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF};
    staticInfo.update(ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES,
                      available_hot_pixel_map_modes,
                      1);


    int32_t avail_min_frame_durations_size = gCamCapability[cameraId]->picture_sizes_tbl_cnt *
                                                 sizeof(scalar_formats)/sizeof(int32_t) * 4;
    int64_t avail_min_frame_durations[avail_min_frame_durations_size];
    int pos = 0;
    for (int j = 0; j < scalar_formats_count; j++) {
        for (int i = 0; i < gCamCapability[cameraId]->picture_sizes_tbl_cnt; i++) {
           avail_min_frame_durations[pos]   = scalar_formats[j];
           avail_min_frame_durations[pos+1] = gCamCapability[cameraId]->picture_sizes_tbl[i].width;
           avail_min_frame_durations[pos+2] = gCamCapability[cameraId]->picture_sizes_tbl[i].height;
           avail_min_frame_durations[pos+3] = NSEC_PER_33MSEC;
           pos+=4;
        }
    }
    staticInfo.update(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
                      avail_min_frame_durations,
                      avail_min_frame_durations_size);

    int32_t request_keys_basic[] = {ANDROID_COLOR_CORRECTION_MODE,
       ANDROID_COLOR_CORRECTION_TRANSFORM, ANDROID_COLOR_CORRECTION_GAINS,
       ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
       ANDROID_CONTROL_AE_ANTIBANDING_MODE, ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
       ANDROID_CONTROL_AE_LOCK, ANDROID_CONTROL_AE_MODE,
       ANDROID_CONTROL_AE_REGIONS, ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
       ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, ANDROID_CONTROL_AF_MODE,
       ANDROID_CONTROL_AF_TRIGGER, ANDROID_CONTROL_AWB_LOCK,
       ANDROID_CONTROL_AWB_MODE, ANDROID_CONTROL_CAPTURE_INTENT,
       ANDROID_CONTROL_EFFECT_MODE, ANDROID_CONTROL_MODE,
       ANDROID_CONTROL_SCENE_MODE, ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
       ANDROID_DEMOSAIC_MODE, ANDROID_EDGE_MODE, ANDROID_EDGE_STRENGTH,
       ANDROID_FLASH_FIRING_POWER, ANDROID_FLASH_FIRING_TIME, ANDROID_FLASH_MODE,
       ANDROID_JPEG_GPS_COORDINATES,
       ANDROID_JPEG_GPS_PROCESSING_METHOD, ANDROID_JPEG_GPS_TIMESTAMP,
       ANDROID_JPEG_ORIENTATION, ANDROID_JPEG_QUALITY, ANDROID_JPEG_THUMBNAIL_QUALITY,
       ANDROID_JPEG_THUMBNAIL_SIZE, ANDROID_LENS_APERTURE, ANDROID_LENS_FILTER_DENSITY,
       ANDROID_LENS_FOCAL_LENGTH, ANDROID_LENS_FOCUS_DISTANCE,
       ANDROID_LENS_OPTICAL_STABILIZATION_MODE, ANDROID_NOISE_REDUCTION_MODE,
       ANDROID_NOISE_REDUCTION_STRENGTH, ANDROID_REQUEST_ID, ANDROID_REQUEST_TYPE,
       ANDROID_SCALER_CROP_REGION, ANDROID_SENSOR_EXPOSURE_TIME,
       ANDROID_SENSOR_FRAME_DURATION,
       ANDROID_SENSOR_SENSITIVITY, ANDROID_SHADING_MODE,
       ANDROID_SHADING_STRENGTH, ANDROID_STATISTICS_FACE_DETECT_MODE,
       ANDROID_STATISTICS_HISTOGRAM_MODE, ANDROID_STATISTICS_SHARPNESS_MAP_MODE,
       ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, ANDROID_TONEMAP_CURVE_BLUE,
       ANDROID_TONEMAP_CURVE_GREEN, ANDROID_TONEMAP_CURVE_RED, ANDROID_TONEMAP_MODE,
       ANDROID_BLACK_LEVEL_LOCK };

    size_t request_keys_cnt =
            sizeof(request_keys_basic)/sizeof(request_keys_basic[0]);
    //NOTE: Please increase available_request_keys array size before
    //adding any new entries.
    int32_t available_request_keys[request_keys_cnt+1];
    memcpy(available_request_keys, request_keys_basic,
            sizeof(request_keys_basic));
    if (gCamCapability[cameraId]->supported_focus_modes_cnt > 1) {
        available_request_keys[request_keys_cnt++] =
                ANDROID_CONTROL_AF_REGIONS;
    }
    //NOTE: Please increase available_request_keys array size before
    //adding any new entries.
    staticInfo.update(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
                      available_request_keys, request_keys_cnt);

    int32_t result_keys_basic[] = {ANDROID_COLOR_CORRECTION_TRANSFORM,
       ANDROID_COLOR_CORRECTION_GAINS, ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
       ANDROID_CONTROL_AE_MODE, ANDROID_CONTROL_AE_REGIONS,
       ANDROID_CONTROL_AE_STATE, ANDROID_CONTROL_AF_MODE,
       ANDROID_CONTROL_AF_STATE, ANDROID_CONTROL_AWB_MODE,
       ANDROID_CONTROL_AWB_STATE, ANDROID_CONTROL_MODE, ANDROID_EDGE_MODE,
       ANDROID_FLASH_FIRING_POWER, ANDROID_FLASH_FIRING_TIME, ANDROID_FLASH_MODE,
       ANDROID_FLASH_STATE, ANDROID_JPEG_GPS_COORDINATES, ANDROID_JPEG_GPS_PROCESSING_METHOD,
       ANDROID_JPEG_GPS_TIMESTAMP, ANDROID_JPEG_ORIENTATION, ANDROID_JPEG_QUALITY,
       ANDROID_JPEG_THUMBNAIL_QUALITY, ANDROID_JPEG_THUMBNAIL_SIZE, ANDROID_LENS_APERTURE,
       ANDROID_LENS_FILTER_DENSITY, ANDROID_LENS_FOCAL_LENGTH, ANDROID_LENS_FOCUS_DISTANCE,
       ANDROID_LENS_FOCUS_RANGE, ANDROID_LENS_STATE, ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
       ANDROID_NOISE_REDUCTION_MODE, ANDROID_REQUEST_ID,
       ANDROID_SCALER_CROP_REGION, ANDROID_SHADING_MODE, ANDROID_SENSOR_EXPOSURE_TIME,
       ANDROID_SENSOR_FRAME_DURATION, ANDROID_SENSOR_SENSITIVITY,
       ANDROID_SENSOR_TIMESTAMP, ANDROID_SENSOR_NEUTRAL_COLOR_POINT,
       ANDROID_SENSOR_PROFILE_TONE_CURVE, ANDROID_BLACK_LEVEL_LOCK, ANDROID_TONEMAP_CURVE_BLUE,
       ANDROID_TONEMAP_CURVE_GREEN, ANDROID_TONEMAP_CURVE_RED, ANDROID_TONEMAP_MODE,
       ANDROID_STATISTICS_FACE_DETECT_MODE, ANDROID_STATISTICS_HISTOGRAM_MODE,
       ANDROID_STATISTICS_SHARPNESS_MAP, ANDROID_STATISTICS_SHARPNESS_MAP_MODE,
       ANDROID_STATISTICS_PREDICTED_COLOR_GAINS, ANDROID_STATISTICS_PREDICTED_COLOR_TRANSFORM,
       ANDROID_STATISTICS_SCENE_FLICKER, ANDROID_STATISTICS_FACE_IDS,
       ANDROID_STATISTICS_FACE_LANDMARKS, ANDROID_STATISTICS_FACE_RECTANGLES,
       ANDROID_STATISTICS_FACE_SCORES};
    size_t result_keys_cnt =
            sizeof(result_keys_basic)/sizeof(result_keys_basic[0]);
    //NOTE: Please increase available_result_keys array size before
    //adding any new entries.
    int32_t available_result_keys[result_keys_cnt+1];
    memcpy(available_result_keys, result_keys_basic,
            sizeof(result_keys_basic));
    if (gCamCapability[cameraId]->supported_focus_modes_cnt > 1) {
        available_result_keys[result_keys_cnt++] =
                ANDROID_CONTROL_AF_REGIONS;
    }
    //NOTE: Please increase available_result_keys array size before
    //adding any new entries.

    staticInfo.update(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
                      available_result_keys, result_keys_cnt);

    int32_t available_characteristics_keys[] = {ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
       ANDROID_CONTROL_AE_AVAILABLE_MODES, ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
       ANDROID_CONTROL_AE_COMPENSATION_RANGE, ANDROID_CONTROL_AE_COMPENSATION_STEP,
       ANDROID_CONTROL_AF_AVAILABLE_MODES, ANDROID_CONTROL_AVAILABLE_EFFECTS,
       ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
       ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
       ANDROID_CONTROL_AWB_AVAILABLE_MODES, ANDROID_CONTROL_MAX_REGIONS,
       ANDROID_CONTROL_SCENE_MODE_OVERRIDES,ANDROID_FLASH_INFO_AVAILABLE,
       ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
       ANDROID_FLASH_INFO_CHARGE_DURATION, ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
       ANDROID_JPEG_MAX_SIZE, ANDROID_LENS_INFO_AVAILABLE_APERTURES,
       ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES,
       ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
       ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
       ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE, ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
       ANDROID_LENS_INFO_SHADING_MAP_SIZE, ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
       ANDROID_LENS_FACING,
       ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS, ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS,
       ANDROID_REQUEST_PIPELINE_MAX_DEPTH, ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
       ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
       ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS, ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
       ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
       ANDROID_SCALER_AVAILABLE_INPUT_OUTPUT_FORMATS_MAP,
       ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
       ANDROID_SCALER_CROPPING_TYPE,
       /*ANDROID_SCALER_AVAILABLE_STALL_DURATIONS,*/
       ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS, ANDROID_SENSOR_FORWARD_MATRIX1,
       ANDROID_SENSOR_REFERENCE_ILLUMINANT1, ANDROID_SENSOR_REFERENCE_ILLUMINANT2,
       ANDROID_SENSOR_FORWARD_MATRIX2, ANDROID_SENSOR_COLOR_TRANSFORM1,
       ANDROID_SENSOR_COLOR_TRANSFORM2, ANDROID_SENSOR_CALIBRATION_TRANSFORM1,
       ANDROID_SENSOR_CALIBRATION_TRANSFORM2, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
       ANDROID_SENSOR_INFO_SENSITIVITY_RANGE, ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT,
       ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE, ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
       ANDROID_SENSOR_INFO_PHYSICAL_SIZE, ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
       ANDROID_SENSOR_INFO_WHITE_LEVEL, ANDROID_SENSOR_BASE_GAIN_FACTOR,
       ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE,
       ANDROID_SENSOR_BLACK_LEVEL_PATTERN, ANDROID_SENSOR_MAX_ANALOG_SENSITIVITY,
       ANDROID_SENSOR_ORIENTATION, ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES,
       ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
       ANDROID_STATISTICS_INFO_HISTOGRAM_BUCKET_COUNT,
       ANDROID_STATISTICS_INFO_MAX_FACE_COUNT, ANDROID_STATISTICS_INFO_MAX_HISTOGRAM_COUNT,
       ANDROID_STATISTICS_INFO_MAX_SHARPNESS_MAP_VALUE,
       ANDROID_STATISTICS_INFO_SHARPNESS_MAP_SIZE, ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES,
       ANDROID_EDGE_AVAILABLE_EDGE_MODES,
       ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
       ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES,
       ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES,
       ANDROID_TONEMAP_MAX_CURVE_POINTS,
       ANDROID_SYNC_MAX_LATENCY,
       ANDROID_CONTROL_AVAILABLE_MODES,
       ANDROID_CONTROL_AE_LOCK_AVAILABLE,
       ANDROID_CONTROL_AWB_LOCK_AVAILABLE,
       ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
       ANDROID_SHADING_AVAILABLE_MODES,
       ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL };
    staticInfo.update(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
                      available_characteristics_keys,
                      sizeof(available_characteristics_keys)/sizeof(int32_t));

    /*available stall durations depend on the hw + sw and will be different for different devices */
    /*have to add for raw after implementation*/
    int32_t stall_formats[] = {HAL_PIXEL_FORMAT_BLOB};
    size_t stall_formats_count = sizeof(stall_formats)/sizeof(int32_t);

    size_t available_stall_size = gCamCapability[cameraId]->picture_sizes_tbl_cnt * 4;
    int64_t available_stall_durations[available_stall_size];
    idx = 0;
    for (uint32_t j = 0; j < stall_formats_count; j++) {
       for (uint32_t i = 0; i < gCamCapability[cameraId]->picture_sizes_tbl_cnt; i++) {
          available_stall_durations[idx]   = stall_formats[j];
          available_stall_durations[idx+1] = gCamCapability[cameraId]->picture_sizes_tbl[i].width;
          available_stall_durations[idx+2] = gCamCapability[cameraId]->picture_sizes_tbl[i].height;
          available_stall_durations[idx+3] = 5 * NSEC_PER_33MSEC;
          idx+=4;
       }
    }
    staticInfo.update(ANDROID_SCALER_AVAILABLE_STALL_DURATIONS,
                      available_stall_durations,
                      idx);

    uint8_t available_correction_modes[] =
        { ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF };
    staticInfo.update(
        ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
        available_correction_modes,
        1);

    uint8_t sensor_timestamp_source[] =
        {ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN};
    staticInfo.update(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE,
                      sensor_timestamp_source,
                      1);

    gStaticMetadata[cameraId] = staticInfo.release();
    return rc;
}

/*===========================================================================
 * FUNCTION   : makeTable
 *
 * DESCRIPTION: make a table of sizes
 *
 * PARAMETERS :
 *
 *
 *==========================================================================*/
void QCamera3HardwareInterface::makeTable(cam_dimension_t* dimTable, uint8_t size,
                                          int32_t* sizeTable)
{
    int j = 0;
    for (int i = 0; i < size; i++) {
        sizeTable[j] = dimTable[i].width;
        sizeTable[j+1] = dimTable[i].height;
        j+=2;
    }
}

/*===========================================================================
 * FUNCTION   : makeFPSTable
 *
 * DESCRIPTION: make a table of fps ranges
 *
 * PARAMETERS :
 *
 *==========================================================================*/
void QCamera3HardwareInterface::makeFPSTable(cam_fps_range_t* fpsTable, uint8_t size,
                                          int32_t* fpsRangesTable)
{
    int j = 0;
    for (int i = 0; i < size; i++) {
        fpsRangesTable[j] = (int32_t)fpsTable[i].min_fps;
        fpsRangesTable[j+1] = (int32_t)fpsTable[i].max_fps;
        j+=2;
    }
}

/*===========================================================================
 * FUNCTION   : makeOverridesList
 *
 * DESCRIPTION: make a list of scene mode overrides
 *
 * PARAMETERS :
 *
 *
 *==========================================================================*/
void QCamera3HardwareInterface::makeOverridesList(cam_scene_mode_overrides_t* overridesTable,
                                                  uint8_t size, uint8_t* overridesList,
                                                  uint8_t* supported_indexes,
                                                  int camera_id)
{
    /*daemon will give a list of overrides for all scene modes.
      However we should send the fwk only the overrides for the scene modes
      supported by the framework*/
    int j = 0, index = 0, supt = 0;
    uint8_t focus_override;
    for (int i = 0; i < size; i++) {
        supt = 0;
        index = supported_indexes[i];
        overridesList[j] = gCamCapability[camera_id]->flash_available ? ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH:ANDROID_CONTROL_AE_MODE_ON;
        overridesList[j+1] = (uint8_t)lookupFwkName(WHITE_BALANCE_MODES_MAP,
                                 sizeof(WHITE_BALANCE_MODES_MAP)/sizeof(WHITE_BALANCE_MODES_MAP[0]),
                                                    overridesTable[index].awb_mode);
        focus_override = (uint8_t)overridesTable[index].af_mode;
        for (int k = 0; k < gCamCapability[camera_id]->supported_focus_modes_cnt; k++) {
           if (gCamCapability[camera_id]->supported_focus_modes[k] == focus_override) {
              supt = 1;
              break;
           }
        }
        if (supt) {
           overridesList[j+2] = (uint8_t)lookupFwkName(FOCUS_MODES_MAP,
                                              sizeof(FOCUS_MODES_MAP)/sizeof(FOCUS_MODES_MAP[0]),
                                              focus_override);
        } else {
           overridesList[j+2] = ANDROID_CONTROL_AF_MODE_OFF;
        }
        j+=3;
    }
}

/*===========================================================================
 * FUNCTION   : getPreviewHalPixelFormat
 *
 * DESCRIPTION: convert the format to type recognized by framework
 *
 * PARAMETERS : format : the format from backend
 *
 ** RETURN    : format recognized by framework
 *
 *==========================================================================*/
int32_t QCamera3HardwareInterface::getScalarFormat(int32_t format)
{
    int32_t halPixelFormat;

    switch (format) {
    case CAM_FORMAT_YUV_420_NV12:
        halPixelFormat = HAL_PIXEL_FORMAT_YCbCr_420_SP;
        break;
    case CAM_FORMAT_YUV_420_NV21:
        halPixelFormat = HAL_PIXEL_FORMAT_YCrCb_420_SP;
        break;
    case CAM_FORMAT_YUV_420_NV21_ADRENO:
        halPixelFormat = HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO;
        break;
    case CAM_FORMAT_YUV_420_YV12:
        halPixelFormat = HAL_PIXEL_FORMAT_YV12;
        break;
    case CAM_FORMAT_YUV_422_NV16:
    case CAM_FORMAT_YUV_422_NV61:
    default:
        halPixelFormat = HAL_PIXEL_FORMAT_YCrCb_420_SP;
        break;
    }
    return halPixelFormat;
}

/*===========================================================================
 * FUNCTION   : getSensorSensitivity
 *
 * DESCRIPTION: convert iso_mode to an integer value
 *
 * PARAMETERS : iso_mode : the iso_mode supported by sensor
 *
 ** RETURN    : sensitivity supported by sensor
 *
 *==========================================================================*/
int32_t QCamera3HardwareInterface::getSensorSensitivity(int32_t iso_mode)
{
    int32_t sensitivity;

    switch (iso_mode) {
    case CAM_ISO_MODE_100:
        sensitivity = 100;
        break;
    case CAM_ISO_MODE_200:
        sensitivity = 200;
        break;
    case CAM_ISO_MODE_400:
        sensitivity = 400;
        break;
    case CAM_ISO_MODE_800:
        sensitivity = 800;
        break;
    case CAM_ISO_MODE_1600:
        sensitivity = 1600;
        break;
    default:
        sensitivity = -1;
        break;
    }
    return sensitivity;
}


/*===========================================================================
 * FUNCTION   : AddSetParmEntryToBatch
 *
 * DESCRIPTION: add set parameter entry into batch
 *
 * PARAMETERS :
 *   @p_table     : ptr to parameter buffer
 *   @paramType   : parameter type
 *   @paramLength : length of parameter value
 *   @paramValue  : ptr to parameter value
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3HardwareInterface::AddSetParmEntryToBatch(parm_buffer_t *p_table,
                                                          cam_intf_parm_type_t paramType,
                                                          uint32_t paramLength,
                                                          void *paramValue)
{
    int position = paramType;
    int current, next;

    /*************************************************************************
    *                 Code to take care of linking next flags                *
    *************************************************************************/
    current = GET_FIRST_PARAM_ID(p_table);
    if (position == current){
        //DO NOTHING
    } else if (position < current){
        SET_NEXT_PARAM_ID(position, p_table, current);
        SET_FIRST_PARAM_ID(p_table, position);
    } else {
        /* Search for the position in the linked list where we need to slot in*/
        while (position > GET_NEXT_PARAM_ID(current, p_table))
            current = GET_NEXT_PARAM_ID(current, p_table);

        /*If node already exists no need to alter linking*/
        if (position != GET_NEXT_PARAM_ID(current, p_table)) {
            next = GET_NEXT_PARAM_ID(current, p_table);
            SET_NEXT_PARAM_ID(current, p_table, position);
            SET_NEXT_PARAM_ID(position, p_table, next);
        }
    }

    /*************************************************************************
    *                   Copy contents into entry                             *
    *************************************************************************/

    if (paramLength > sizeof(parm_type_t)) {
        ALOGE("%s:Size of input larger than max entry size",__func__);
        return BAD_VALUE;
    }
    memcpy(POINTER_OF(paramType,p_table), paramValue, paramLength);
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : lookupFwkName
 *
 * DESCRIPTION: In case the enum is not same in fwk and backend
 *              make sure the parameter is correctly propogated
 *
 * PARAMETERS  :
 *   @arr      : map between the two enums
 *   @len      : len of the map
 *   @hal_name : name of the hal_parm to map
 *
 * RETURN     : int type of status
 *              fwk_name  -- success
 *              none-zero failure code
 *==========================================================================*/
int8_t QCamera3HardwareInterface::lookupFwkName(const QCameraMap arr[],
                                             int len, int hal_name)
{

    for (int i = 0; i < len; i++) {
        if (arr[i].hal_name == hal_name)
            return arr[i].fwk_name;
    }

    /* Not able to find matching framework type is not necessarily
     * an error case. This happens when mm-camera supports more attributes
     * than the frameworks do */
    ALOGD("%s: Cannot find matching framework type", __func__);
    return NAME_NOT_FOUND;
}

/*===========================================================================
 * FUNCTION   : lookupHalName
 *
 * DESCRIPTION: In case the enum is not same in fwk and backend
 *              make sure the parameter is correctly propogated
 *
 * PARAMETERS  :
 *   @arr      : map between the two enums
 *   @len      : len of the map
 *   @fwk_name : name of the hal_parm to map
 *
 * RETURN     : int32_t type of status
 *              hal_name  -- success
 *              none-zero failure code
 *==========================================================================*/
int8_t QCamera3HardwareInterface::lookupHalName(const QCameraMap arr[],
                                             int len, int fwk_name)
{
    for (int i = 0; i < len; i++) {
       if (arr[i].fwk_name == fwk_name)
           return arr[i].hal_name;
    }
    ALOGE("%s: Cannot find matching hal type", __func__);
    return NAME_NOT_FOUND;
}

/*===========================================================================
 * FUNCTION   : getCapabilities
 *
 * DESCRIPTION: query camera capabilities
 *
 * PARAMETERS :
 *   @cameraId  : camera Id
 *   @info      : camera info struct to be filled in with camera capabilities
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera3HardwareInterface::getCamInfo(int cameraId,
                                    struct camera_info *info)
{
    int rc = 0;

    if (NULL == gCamCapability[cameraId]) {
        rc = initCapabilities(cameraId);
        if (rc < 0) {
            //pthread_mutex_unlock(&g_camlock);
            return rc;
        }
    }

    if (NULL == gStaticMetadata[cameraId]) {
        rc = initStaticMetadata(cameraId);
        if (rc < 0) {
            return rc;
        }
    }

    switch(gCamCapability[cameraId]->position) {
    case CAM_POSITION_BACK:
        info->facing = CAMERA_FACING_BACK;
        break;

    case CAM_POSITION_FRONT:
        info->facing = CAMERA_FACING_FRONT;
        break;

    default:
        ALOGE("%s:Unknown position type for camera id:%d", __func__, cameraId);
        rc = -1;
        break;
    }


    info->orientation = gCamCapability[cameraId]->sensor_mount_angle;
    info->device_version = CAMERA_DEVICE_API_VERSION_3_2;
    info->static_camera_characteristics = gStaticMetadata[cameraId];

    return rc;
}

/*===========================================================================
 * FUNCTION   : translateMetadata
 *
 * DESCRIPTION: translate the metadata into camera_metadata_t
 *
 * PARAMETERS : type of the request
 *
 *
 * RETURN     : success: camera_metadata_t*
 *              failure: NULL
 *
 *==========================================================================*/
camera_metadata_t* QCamera3HardwareInterface::translateCapabilityToMetadata(int type)
{
    pthread_mutex_lock(&mMutex);

    if (mDefaultMetadata[type] != NULL) {
        pthread_mutex_unlock(&mMutex);
        return mDefaultMetadata[type];
    }
    //first time we are handling this request
    //fill up the metadata structure using the wrapper class
    CameraMetadata settings;
    //translate from cam_capability_t to camera_metadata_tag_t
    static const uint8_t requestType = ANDROID_REQUEST_TYPE_CAPTURE;
    settings.update(ANDROID_REQUEST_TYPE, &requestType, 1);
    int32_t defaultRequestID = 0;
    settings.update(ANDROID_REQUEST_ID, &defaultRequestID, 1);

    /*control*/

    uint8_t controlIntent = 0;
    uint8_t focusMode;
    uint8_t edge_mode;
    uint8_t noise_red_mode;
    uint8_t tonemap_mode;
    switch (type) {
      case CAMERA3_TEMPLATE_PREVIEW:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
        focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        edge_mode = ANDROID_EDGE_MODE_FAST;
        noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_FAST;
        tonemap_mode = ANDROID_TONEMAP_MODE_FAST;
        break;
      case CAMERA3_TEMPLATE_STILL_CAPTURE:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE;
        focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        edge_mode = ANDROID_EDGE_MODE_HIGH_QUALITY;
        noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY;
        tonemap_mode = ANDROID_TONEMAP_MODE_HIGH_QUALITY;
        break;
      case CAMERA3_TEMPLATE_VIDEO_RECORD:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD;
        focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
        edge_mode = ANDROID_EDGE_MODE_FAST;
        noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_FAST;
        tonemap_mode = ANDROID_TONEMAP_MODE_FAST;
        break;
      case CAMERA3_TEMPLATE_VIDEO_SNAPSHOT:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT;
        focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
        edge_mode = ANDROID_EDGE_MODE_FAST;
        noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_FAST;
        tonemap_mode = ANDROID_TONEMAP_MODE_FAST;
        break;
      case CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG;
        focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        edge_mode = ANDROID_EDGE_MODE_FAST;
        noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_FAST;
        tonemap_mode = ANDROID_TONEMAP_MODE_FAST;
        break;
      case CAMERA3_TEMPLATE_MANUAL:
        edge_mode = ANDROID_EDGE_MODE_FAST;
        noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_FAST;
        tonemap_mode = ANDROID_TONEMAP_MODE_FAST;
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_MANUAL;
        focusMode = ANDROID_CONTROL_AF_MODE_OFF;
        break;
      default:
        edge_mode = ANDROID_EDGE_MODE_FAST;
        noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_FAST;
        tonemap_mode = ANDROID_TONEMAP_MODE_FAST;
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_CUSTOM;
        break;
    }
    settings.update(ANDROID_CONTROL_CAPTURE_INTENT, &controlIntent, 1);

    static const uint8_t cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF;
    settings.update(ANDROID_COLOR_CORRECTION_ABERRATION_MODE, &cacMode, 1);

    if (gCamCapability[mCameraId]->supported_focus_modes_cnt == 1) {
        focusMode = ANDROID_CONTROL_AF_MODE_OFF;
    }
    settings.update(ANDROID_CONTROL_AF_MODE, &focusMode, 1);

    settings.update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
            &gCamCapability[mCameraId]->exposure_compensation_default, 1);

    static const uint8_t aeLock = ANDROID_CONTROL_AE_LOCK_OFF;
    settings.update(ANDROID_CONTROL_AE_LOCK, &aeLock, 1);

    static const uint8_t awbLock = ANDROID_CONTROL_AWB_LOCK_OFF;
    settings.update(ANDROID_CONTROL_AWB_LOCK, &awbLock, 1);

    static const uint8_t awbMode = ANDROID_CONTROL_AWB_MODE_AUTO;
    settings.update(ANDROID_CONTROL_AWB_MODE, &awbMode, 1);

    static const uint8_t controlMode = ANDROID_CONTROL_MODE_AUTO;
    settings.update(ANDROID_CONTROL_MODE, &controlMode, 1);

    static const uint8_t effectMode = ANDROID_CONTROL_EFFECT_MODE_OFF;
    settings.update(ANDROID_CONTROL_EFFECT_MODE, &effectMode, 1);

    static const uint8_t sceneMode = ANDROID_CONTROL_SCENE_MODE_FACE_PRIORITY;
    settings.update(ANDROID_CONTROL_SCENE_MODE, &sceneMode, 1);

    static const uint8_t aeMode = ANDROID_CONTROL_AE_MODE_ON;
    settings.update(ANDROID_CONTROL_AE_MODE, &aeMode, 1);

    /*flash*/
    static const uint8_t flashMode = ANDROID_FLASH_MODE_OFF;
    settings.update(ANDROID_FLASH_MODE, &flashMode, 1);

    static const uint8_t flashFiringLevel = CAM_FLASH_FIRING_LEVEL_4;
    settings.update(ANDROID_FLASH_FIRING_POWER,
            &flashFiringLevel, 1);

    /* lens */
    float default_aperture = gCamCapability[mCameraId]->apertures[0];
    settings.update(ANDROID_LENS_APERTURE, &default_aperture, 1);

    if (gCamCapability[mCameraId]->filter_densities_count) {
        float default_filter_density = gCamCapability[mCameraId]->filter_densities[0];
        settings.update(ANDROID_LENS_FILTER_DENSITY, &default_filter_density,
                        gCamCapability[mCameraId]->filter_densities_count);
    }

    float default_focal_length = gCamCapability[mCameraId]->focal_length;
    settings.update(ANDROID_LENS_FOCAL_LENGTH, &default_focal_length, 1);

    static const float default_focus_distance = 0;
    settings.update(ANDROID_LENS_FOCUS_DISTANCE, &default_focus_distance, 1);

    static const uint8_t demosaicMode = ANDROID_DEMOSAIC_MODE_FAST;
    settings.update(ANDROID_DEMOSAIC_MODE, &demosaicMode, 1);

    static const uint8_t hotpixelMode = ANDROID_HOT_PIXEL_MODE_FAST;
    settings.update(ANDROID_HOT_PIXEL_MODE, &hotpixelMode, 1);

    static const int32_t testpatternMode = ANDROID_SENSOR_TEST_PATTERN_MODE_OFF;
    settings.update(ANDROID_SENSOR_TEST_PATTERN_MODE, &testpatternMode, 1);

    static const uint8_t faceDetectMode = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
    settings.update(ANDROID_STATISTICS_FACE_DETECT_MODE, &faceDetectMode, 1);

    static const uint8_t histogramMode = ANDROID_STATISTICS_HISTOGRAM_MODE_OFF;
    settings.update(ANDROID_STATISTICS_HISTOGRAM_MODE, &histogramMode, 1);

    static const uint8_t sharpnessMapMode = ANDROID_STATISTICS_SHARPNESS_MAP_MODE_OFF;
    settings.update(ANDROID_STATISTICS_SHARPNESS_MAP_MODE, &sharpnessMapMode, 1);

    static const uint8_t hotPixelMapMode = ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF;
    settings.update(ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE, &hotPixelMapMode, 1);

    static const  uint8_t shadingMapMode = ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF;
    settings.update(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, &shadingMapMode, 1);

    static const uint8_t blackLevelLock = ANDROID_BLACK_LEVEL_LOCK_OFF;
    settings.update(ANDROID_BLACK_LEVEL_LOCK, &blackLevelLock, 1);

    /* Exposure time(Update the Min Exposure Time)*/
    int64_t default_exposure_time = gCamCapability[mCameraId]->exposure_time_range[0];
    settings.update(ANDROID_SENSOR_EXPOSURE_TIME, &default_exposure_time, 1);

    /* frame duration */
    static const int64_t default_frame_duration = NSEC_PER_SEC / 30;
    settings.update(ANDROID_SENSOR_FRAME_DURATION, &default_frame_duration, 1);

    /* sensitivity */
    static const int32_t default_sensitivity = 100;
    settings.update(ANDROID_SENSOR_SENSITIVITY, &default_sensitivity, 1);

    /*edge mode*/
    settings.update(ANDROID_EDGE_MODE, &edge_mode, 1);

    /*noise reduction mode*/
    settings.update(ANDROID_NOISE_REDUCTION_MODE, &noise_red_mode, 1);

    /*color correction mode*/
    static const uint8_t color_correct_mode = ANDROID_COLOR_CORRECTION_MODE_FAST;
    settings.update(ANDROID_COLOR_CORRECTION_MODE, &color_correct_mode, 1);

    /*transform matrix mode*/
    settings.update(ANDROID_TONEMAP_MODE, &tonemap_mode, 1);

    int32_t scaler_crop_region[4];
    scaler_crop_region[0] = 0;
    scaler_crop_region[1] = 0;
    scaler_crop_region[2] = gCamCapability[mCameraId]->active_array_size.width;
    scaler_crop_region[3] = gCamCapability[mCameraId]->active_array_size.height;
    settings.update(ANDROID_SCALER_CROP_REGION, scaler_crop_region, 4);

    static const uint8_t antibanding_mode = ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
    settings.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &antibanding_mode, 1);

    static const uint8_t vs_mode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF;
    settings.update(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &vs_mode, 1);

    uint8_t opt_stab_mode = (gCamCapability[mCameraId]->optical_stab_modes_count == 2)?
                             ANDROID_LENS_OPTICAL_STABILIZATION_MODE_ON :
                             ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    settings.update(ANDROID_LENS_OPTICAL_STABILIZATION_MODE, &opt_stab_mode, 1);

    /*focus distance*/
    static const float focus_distance = 0.0;
    settings.update(ANDROID_LENS_FOCUS_DISTANCE, &focus_distance, 1);

    /*target fps range: use maximum range for picture, and maximum fixed range for video*/
    float max_range = 0.0;
    float max_fixed_fps = 0.0;
    int32_t fps_range[2] = {0, 0};
    for (uint32_t i = 0; i < gCamCapability[mCameraId]->fps_ranges_tbl_cnt;
            i++) {
        float range = gCamCapability[mCameraId]->fps_ranges_tbl[i].max_fps -
            gCamCapability[mCameraId]->fps_ranges_tbl[i].min_fps;
        if (type == CAMERA3_TEMPLATE_PREVIEW ||
                type == CAMERA3_TEMPLATE_STILL_CAPTURE ||
                type == CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG) {
            if (range > max_range) {
                fps_range[0] =
                    (int32_t)gCamCapability[mCameraId]->fps_ranges_tbl[i].min_fps;
                fps_range[1] =
                    (int32_t)gCamCapability[mCameraId]->fps_ranges_tbl[i].max_fps;
                max_range = range;
            }
        } else {
            if (range < 0.01 && max_fixed_fps <
                    gCamCapability[mCameraId]->fps_ranges_tbl[i].max_fps) {
                fps_range[0] =
                    (int32_t)gCamCapability[mCameraId]->fps_ranges_tbl[i].min_fps;
                fps_range[1] =
                    (int32_t)gCamCapability[mCameraId]->fps_ranges_tbl[i].max_fps;
                max_fixed_fps = gCamCapability[mCameraId]->fps_ranges_tbl[i].max_fps;
            }
        }
    }
    settings.update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, fps_range, 2);

    /*precapture trigger*/
    uint8_t precapture_trigger = ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
    settings.update(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &precapture_trigger, 1);

    /*af trigger*/
    uint8_t af_trigger = ANDROID_CONTROL_AF_TRIGGER_IDLE;
    settings.update(ANDROID_CONTROL_AF_TRIGGER, &af_trigger, 1);

    /* ae & af regions */
    int32_t active_region[] = {
            gCamCapability[mCameraId]->active_array_size.left,
            gCamCapability[mCameraId]->active_array_size.top,
            gCamCapability[mCameraId]->active_array_size.left +
                    gCamCapability[mCameraId]->active_array_size.width,
            gCamCapability[mCameraId]->active_array_size.top +
                    gCamCapability[mCameraId]->active_array_size.height,
            0};
    settings.update(ANDROID_CONTROL_AE_REGIONS, active_region, 5);
    settings.update(ANDROID_CONTROL_AF_REGIONS, active_region, 5);

    /* black level lock */
    uint8_t blacklevel_lock = ANDROID_BLACK_LEVEL_LOCK_OFF;
    settings.update(ANDROID_BLACK_LEVEL_LOCK, &blacklevel_lock, 1);

    //special defaults for manual template
    if (type == CAMERA3_TEMPLATE_MANUAL) {
        static const uint8_t manualControlMode = ANDROID_CONTROL_MODE_OFF;
        settings.update(ANDROID_CONTROL_MODE, &manualControlMode, 1);

        static const uint8_t manualFocusMode = ANDROID_CONTROL_AF_MODE_OFF;
        settings.update(ANDROID_CONTROL_AF_MODE, &manualFocusMode, 1);

        static const uint8_t manualAeMode = ANDROID_CONTROL_AE_MODE_OFF;
        settings.update(ANDROID_CONTROL_AE_MODE, &manualAeMode, 1);

        static const uint8_t manualAwbMode = ANDROID_CONTROL_AWB_MODE_OFF;
        settings.update(ANDROID_CONTROL_AWB_MODE, &manualAwbMode, 1);

        static const uint8_t manualTonemapMode = ANDROID_TONEMAP_MODE_FAST;
        settings.update(ANDROID_TONEMAP_MODE, &manualTonemapMode, 1);

        static const uint8_t manualColorCorrectMode = ANDROID_COLOR_CORRECTION_MODE_TRANSFORM_MATRIX;
        settings.update(ANDROID_COLOR_CORRECTION_MODE, &manualColorCorrectMode, 1);
    }

    mDefaultMetadata[type] = settings.release();

    pthread_mutex_unlock(&mMutex);
    return mDefaultMetadata[type];
}

/*===========================================================================
 * FUNCTION   : setFrameParameters
 *
 * DESCRIPTION: set parameters per frame as requested in the metadata from
 *              framework
 *
 * PARAMETERS :
 *   @frame_id  : frame number for this particular request
 *   @settings  : frame settings information from framework
 *   @streamTypeMask : bit mask of stream types on which buffers are requested
 *   @aeTrigger : Return aeTrigger if it exists in the request
 *
 * RETURN     : success: NO_ERROR
 *              failure:
 *==========================================================================*/
int QCamera3HardwareInterface::setFrameParameters(int frame_id,
        const camera_metadata_t *settings, uint32_t streamTypeMask,
        cam_trigger_t &aeTrigger)
{
    /*translate from camera_metadata_t type to parm_type_t*/
    int rc = 0;
    if (settings == NULL && mFirstRequest) {
        /*settings cannot be null for the first request*/
        return BAD_VALUE;
    }

    int32_t hal_version = CAM_HAL_V3;

    memset(mParameters, 0, sizeof(parm_buffer_t));
    mParameters->first_flagged_entry = CAM_INTF_PARM_MAX;
    AddSetParmEntryToBatch(mParameters, CAM_INTF_PARM_HAL_VERSION,
                sizeof(hal_version), &hal_version);

    /*we need to update the frame number in the parameters*/
    rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_META_FRAME_NUMBER,
                                sizeof(frame_id), &frame_id);
    if (rc < 0) {
        ALOGE("%s: Failed to set the frame number in the parameters", __func__);
        return BAD_VALUE;
    }

    /* Update stream id mask where buffers are requested */
    rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_META_STREAM_TYPE_MASK,
                                sizeof(streamTypeMask), &streamTypeMask);
    if (rc < 0) {
        ALOGE("%s: Failed to set stream type mask in the parameters", __func__);
        return BAD_VALUE;
    }

    if(settings != NULL){
        rc = translateMetadataToParameters(settings, aeTrigger);
    }
    /*set the parameters to backend*/
    mCameraHandle->ops->set_parms(mCameraHandle->camera_handle, mParameters);
    return rc;
}

/*===========================================================================
 * FUNCTION   : translateMetadataToParameters
 *
 * DESCRIPTION: read from the camera_metadata_t and change to parm_type_t
 *
 *
 * PARAMETERS :
 *   @settings  : frame settings information from framework
 *   @aeTrigger : output ae trigger if it's set in request
 *
 * RETURN     : success: NO_ERROR
 *              failure:
 *==========================================================================*/
int QCamera3HardwareInterface::translateMetadataToParameters(
        const camera_metadata_t *settings, cam_trigger_t &aeTrigger)
{
    int rc = 0;
    CameraMetadata frame_settings;
    frame_settings = settings;


    if (frame_settings.exists(ANDROID_CONTROL_AE_ANTIBANDING_MODE)) {
        int32_t antibandingMode =
            frame_settings.find(ANDROID_CONTROL_AE_ANTIBANDING_MODE).data.i32[0];
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_PARM_ANTIBANDING,
                sizeof(antibandingMode), &antibandingMode);
    }

    if (frame_settings.exists(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION)) {
        int32_t expCompensation = frame_settings.find(
            ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION).data.i32[0];
        if (expCompensation < gCamCapability[mCameraId]->exposure_compensation_min)
            expCompensation = gCamCapability[mCameraId]->exposure_compensation_min;
        if (expCompensation > gCamCapability[mCameraId]->exposure_compensation_max)
            expCompensation = gCamCapability[mCameraId]->exposure_compensation_max;
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_PARM_EXPOSURE_COMPENSATION,
          sizeof(expCompensation), &expCompensation);
    }

    if (frame_settings.exists(ANDROID_CONTROL_AE_LOCK)) {
        mAeLock = frame_settings.find(ANDROID_CONTROL_AE_LOCK).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_PARM_AEC_LOCK,
                sizeof(mAeLock), &mAeLock);
    }
    if (frame_settings.exists(ANDROID_CONTROL_AE_TARGET_FPS_RANGE)) {
        cam_fps_range_t fps_range;
        fps_range.min_fps =
            frame_settings.find(ANDROID_CONTROL_AE_TARGET_FPS_RANGE).data.i32[0];
        fps_range.max_fps =
            frame_settings.find(ANDROID_CONTROL_AE_TARGET_FPS_RANGE).data.i32[1];
        mSensorFrameDuration = NSEC_PER_SEC / fps_range.max_fps;
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_PARM_FPS_RANGE,
                sizeof(fps_range), &fps_range);
    }

    float focalDistance = -1.0;
    if (frame_settings.exists(ANDROID_LENS_FOCUS_DISTANCE)) {
        focalDistance = frame_settings.find(ANDROID_LENS_FOCUS_DISTANCE).data.f[0];
        rc = AddSetParmEntryToBatch(mParameters,
                CAM_INTF_META_LENS_FOCUS_DISTANCE,
                sizeof(focalDistance), &focalDistance);
    }

    if (frame_settings.exists(ANDROID_CONTROL_AF_MODE)) {
        mAfMode = frame_settings.find(ANDROID_CONTROL_AF_MODE).data.u8[0];
        uint8_t focusMode;
        if (focalDistance == 0.0 && mAfMode == ANDROID_CONTROL_AF_MODE_OFF) {
            focusMode = CAM_FOCUS_MODE_INFINITY;
        } else{
         focusMode = lookupHalName(FOCUS_MODES_MAP,
                                   sizeof(FOCUS_MODES_MAP),
                                   mAfMode);
        }
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_PARM_FOCUS_MODE,
                sizeof(focusMode), &focusMode);
    }

    if (frame_settings.exists(ANDROID_CONTROL_AWB_LOCK)) {
        mAwbLock = frame_settings.find(ANDROID_CONTROL_AWB_LOCK).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_PARM_AWB_LOCK,
                sizeof(mAwbLock), &mAwbLock);
    }

    if (frame_settings.exists(ANDROID_CONTROL_AWB_MODE)) {
        mAwbMode = frame_settings.find(ANDROID_CONTROL_AWB_MODE).data.u8[0];
        uint8_t whiteLevel = lookupHalName(WHITE_BALANCE_MODES_MAP,
                sizeof(WHITE_BALANCE_MODES_MAP),
                mAwbMode);
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_PARM_WHITE_BALANCE,
                sizeof(whiteLevel), &whiteLevel);
    }

    if (frame_settings.exists(ANDROID_CONTROL_EFFECT_MODE)) {
        mEffectMode =
            frame_settings.find(ANDROID_CONTROL_EFFECT_MODE).data.u8[0];
        uint8_t effectMode = lookupHalName(EFFECT_MODES_MAP,
                sizeof(EFFECT_MODES_MAP),
                mEffectMode);
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_PARM_EFFECT,
                sizeof(effectMode), &effectMode);
    }

    if (frame_settings.exists(ANDROID_CONTROL_AE_MODE)) {
        mAeMode = frame_settings.find(ANDROID_CONTROL_AE_MODE).data.u8[0];
        uint8_t aeMode;
        int32_t redeye;

        if (mAeMode == ANDROID_CONTROL_AE_MODE_OFF ) {
            aeMode = CAM_AE_MODE_OFF;
        } else {
            aeMode = CAM_AE_MODE_ON;
        }
        if (mAeMode == ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH_REDEYE) {
            redeye = 1;
        } else {
            redeye = 0;
        }

        int32_t flashMode = (int32_t)lookupHalName(AE_FLASH_MODE_MAP,
                                          sizeof(AE_FLASH_MODE_MAP),
                                          mAeMode);
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_META_AEC_MODE,
                sizeof(aeMode), &aeMode);
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_PARM_LED_MODE,
                sizeof(flashMode), &flashMode);
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_PARM_REDEYE_REDUCTION,
                sizeof(redeye), &redeye);
    }

    if (frame_settings.exists(ANDROID_COLOR_CORRECTION_MODE)) {
        mColorCorrectMode =
            frame_settings.find(ANDROID_COLOR_CORRECTION_MODE).data.u8[0];
        ALOGI("Setting ANDROID_COLOR_CORRECTION_MODE=%d", mColorCorrectMode);
        rc =
            AddSetParmEntryToBatch(mParameters, CAM_INTF_META_COLOR_CORRECT_MODE,
                    sizeof(mColorCorrectMode), &mColorCorrectMode);
    }

    if (frame_settings.exists(ANDROID_COLOR_CORRECTION_GAINS)) {
        for (int i = 0; i < 4; i++) {
            mColorCorrectGains.gains[i] =
                frame_settings.find(ANDROID_COLOR_CORRECTION_GAINS).data.f[i];
            ALOGI("Setting ANDROID_COLOR_CORRECTION_GAINS %d=%f", i, mColorCorrectGains.gains[i]);
        }
        rc =
            AddSetParmEntryToBatch(mParameters, CAM_INTF_META_COLOR_CORRECT_GAINS,
                    sizeof(mColorCorrectGains), &mColorCorrectGains);
    }

    if (frame_settings.exists(ANDROID_COLOR_CORRECTION_TRANSFORM)) {
        cam_color_correct_matrix_t colorCorrectTransform;
        cam_rational_type_t transform_elem;
        int num = 0;
        for (int i = 0; i < 3; i++) {
           for (int j = 0; j < 3; j++) {
              transform_elem.numerator =
                 frame_settings.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[num].numerator;
              transform_elem.denominator =
                 frame_settings.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[num].denominator;
              colorCorrectTransform.transform_matrix[i][j] = transform_elem;
              num++;
           }
        }
        rc =
            AddSetParmEntryToBatch(mParameters, CAM_INTF_META_COLOR_CORRECT_TRANSFORM,
                    sizeof(colorCorrectTransform), &colorCorrectTransform);
    }

    if (frame_settings.exists(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER)&&
        frame_settings.exists(ANDROID_CONTROL_AE_PRECAPTURE_ID)) {
        aeTrigger.trigger =
            frame_settings.find(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER).data.u8[0];
        aeTrigger.trigger_id =
            frame_settings.find(ANDROID_CONTROL_AE_PRECAPTURE_ID).data.i32[0];
        mPrecaptureId = aeTrigger.trigger_id;
    }
    rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_META_AEC_PRECAPTURE_TRIGGER,
                                sizeof(aeTrigger), &aeTrigger);

    /*af_trigger must come with a trigger id*/
    if (frame_settings.exists(ANDROID_CONTROL_AF_TRIGGER) &&
        frame_settings.exists(ANDROID_CONTROL_AF_TRIGGER_ID)) {
        mAfTrigger.trigger =
            frame_settings.find(ANDROID_CONTROL_AF_TRIGGER).data.u8[0];
        mAfTrigger.trigger_id =
            frame_settings.find(ANDROID_CONTROL_AF_TRIGGER_ID).data.i32[0];
        rc = AddSetParmEntryToBatch(mParameters,
                CAM_INTF_META_AF_TRIGGER, sizeof(mAfTrigger), &mAfTrigger);
    }

    if (frame_settings.exists(ANDROID_CONTROL_MODE)) {
        mControlMode = frame_settings.find(ANDROID_CONTROL_MODE).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_META_MODE,
                sizeof(mControlMode), &mControlMode);
        if (mControlMode == ANDROID_CONTROL_MODE_USE_SCENE_MODE) {
           mSceneMode = frame_settings.find(ANDROID_CONTROL_SCENE_MODE).data.u8[0];
           uint8_t sceneMode = lookupHalName(SCENE_MODES_MAP,
                                             sizeof(SCENE_MODES_MAP)/sizeof(SCENE_MODES_MAP[0]),
                                             mSceneMode);
           rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_PARM_BESTSHOT_MODE,
                sizeof(sceneMode), &sceneMode);
        } else if (mControlMode == ANDROID_CONTROL_MODE_OFF) {
           uint8_t sceneMode = CAM_SCENE_MODE_OFF;
           rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_PARM_BESTSHOT_MODE,
                sizeof(sceneMode), &sceneMode);
        } else if (mControlMode == ANDROID_CONTROL_MODE_AUTO) {
           uint8_t sceneMode = CAM_SCENE_MODE_OFF;
           rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_PARM_BESTSHOT_MODE,
                sizeof(sceneMode), &sceneMode);
        }
    }

    if (frame_settings.exists(ANDROID_DEMOSAIC_MODE)) {
        int32_t demosaic =
            frame_settings.find(ANDROID_DEMOSAIC_MODE).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_META_DEMOSAIC,
                sizeof(demosaic), &demosaic);
    }

    if (frame_settings.exists(ANDROID_EDGE_MODE)) {
        mEdgeMode = frame_settings.find(ANDROID_EDGE_MODE).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_META_EDGE_MODE,
                sizeof(mEdgeMode), &mEdgeMode);
    }

    if (frame_settings.exists(ANDROID_EDGE_STRENGTH)) {
        int32_t edgeStrength =
            frame_settings.find(ANDROID_EDGE_STRENGTH).data.i32[0];
        rc = AddSetParmEntryToBatch(mParameters,
                CAM_INTF_META_SHARPNESS_STRENGTH, sizeof(edgeStrength), &edgeStrength);
    }

    if (frame_settings.exists(ANDROID_FLASH_MODE)) {
        int32_t respectFlashMode = 1;
        if (frame_settings.exists(ANDROID_CONTROL_AE_MODE)) {
            uint8_t fwk_aeMode =
                frame_settings.find(ANDROID_CONTROL_AE_MODE).data.u8[0];
            if (fwk_aeMode > ANDROID_CONTROL_AE_MODE_ON) {
                respectFlashMode = 0;
                ALOGV("%s: AE Mode controls flash, ignore android.flash.mode",
                    __func__);
            }
        }
        if (respectFlashMode) {
            uint8_t flashMode =
                frame_settings.find(ANDROID_FLASH_MODE).data.u8[0];
            flashMode = (int32_t)lookupHalName(FLASH_MODES_MAP,
                                          sizeof(FLASH_MODES_MAP),
                                          flashMode);
            ALOGV("%s: flash mode after mapping %d", __func__, flashMode);
            // To check: CAM_INTF_META_FLASH_MODE usage
            rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_PARM_LED_MODE,
                          sizeof(flashMode), &flashMode);
        }
    }

    if (frame_settings.exists(ANDROID_FLASH_FIRING_POWER)) {
        uint8_t flashPower =
            frame_settings.find(ANDROID_FLASH_FIRING_POWER).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_META_FLASH_POWER,
                sizeof(flashPower), &flashPower);
    }

    if (frame_settings.exists(ANDROID_FLASH_FIRING_TIME)) {
        int64_t flashFiringTime =
            frame_settings.find(ANDROID_FLASH_FIRING_TIME).data.i64[0];
        rc = AddSetParmEntryToBatch(mParameters,
                CAM_INTF_META_FLASH_FIRING_TIME, sizeof(flashFiringTime), &flashFiringTime);
    }

    if (frame_settings.exists(ANDROID_HOT_PIXEL_MODE)) {
        uint8_t hotPixelMode =
            frame_settings.find(ANDROID_HOT_PIXEL_MODE).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_META_HOTPIXEL_MODE,
                sizeof(hotPixelMode), &hotPixelMode);
    }

    if (frame_settings.exists(ANDROID_LENS_APERTURE)) {
        float lensAperture =
            frame_settings.find( ANDROID_LENS_APERTURE).data.f[0];
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_META_LENS_APERTURE,
                sizeof(lensAperture), &lensAperture);
    }

    if (frame_settings.exists(ANDROID_LENS_FILTER_DENSITY)) {
        float filterDensity =
            frame_settings.find(ANDROID_LENS_FILTER_DENSITY).data.f[0];
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_META_LENS_FILTERDENSITY,
                sizeof(filterDensity), &filterDensity);
    }

    if (frame_settings.exists(ANDROID_LENS_FOCAL_LENGTH)) {
        float focalLength =
            frame_settings.find(ANDROID_LENS_FOCAL_LENGTH).data.f[0];
        rc = AddSetParmEntryToBatch(mParameters,
                CAM_INTF_META_LENS_FOCAL_LENGTH,
                sizeof(focalLength), &focalLength);
    }

    if (frame_settings.exists(ANDROID_LENS_OPTICAL_STABILIZATION_MODE)) {
        uint8_t optStabMode =
            frame_settings.find(ANDROID_LENS_OPTICAL_STABILIZATION_MODE).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters,
                CAM_INTF_META_LENS_OPT_STAB_MODE,
                sizeof(optStabMode), &optStabMode);
    }

    if (frame_settings.exists(ANDROID_NOISE_REDUCTION_MODE)) {
        mNoiseReductionMode =
            frame_settings.find(ANDROID_NOISE_REDUCTION_MODE).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters,
                CAM_INTF_META_NOISE_REDUCTION_MODE,
                sizeof(mNoiseReductionMode), &mNoiseReductionMode);
    }

    if (frame_settings.exists(ANDROID_NOISE_REDUCTION_STRENGTH)) {
        uint8_t noiseRedStrength =
            frame_settings.find(ANDROID_NOISE_REDUCTION_STRENGTH).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters,
                CAM_INTF_META_NOISE_REDUCTION_STRENGTH,
                sizeof(noiseRedStrength), &noiseRedStrength);
    }

    cam_crop_region_t scalerCropRegion;
    bool scalerCropSet = false;
    if (frame_settings.exists(ANDROID_SCALER_CROP_REGION)) {
        scalerCropRegion.left =
            frame_settings.find(ANDROID_SCALER_CROP_REGION).data.i32[0];
        scalerCropRegion.top =
            frame_settings.find(ANDROID_SCALER_CROP_REGION).data.i32[1];
        scalerCropRegion.width =
            frame_settings.find(ANDROID_SCALER_CROP_REGION).data.i32[2];
        scalerCropRegion.height =
            frame_settings.find(ANDROID_SCALER_CROP_REGION).data.i32[3];
        rc = AddSetParmEntryToBatch(mParameters,
                CAM_INTF_META_SCALER_CROP_REGION,
                sizeof(scalerCropRegion), &scalerCropRegion);
        scalerCropSet = true;
    }

    if (frame_settings.exists(ANDROID_SENSOR_EXPOSURE_TIME)) {
        int64_t sensorExpTime =
            frame_settings.find(ANDROID_SENSOR_EXPOSURE_TIME).data.i64[0];
        rc = AddSetParmEntryToBatch(mParameters,
                CAM_INTF_META_SENSOR_EXPOSURE_TIME,
                sizeof(sensorExpTime), &sensorExpTime);
    }

#if 0
    if (frame_settings.exists(ANDROID_SENSOR_FRAME_DURATION)) {
        mSensorFrameDuration =
            frame_settings.find(ANDROID_SENSOR_FRAME_DURATION).data.i64[0];
        ALOGI("ANDROID_SENSOR_FRAME_DURATION setting exists: %lld", mSensorFrameDuration);
        if (gCamCapability[mCameraId]->max_frame_duration > 0 &&
            mSensorFrameDuration > gCamCapability[mCameraId]->max_frame_duration)
            mSensorFrameDuration = gCamCapability[mCameraId]->max_frame_duration;
        //rc = AddSetParmEntryToBatch(mParameters,
        //        CAM_INTF_META_SENSOR_FRAME_DURATION,
        //        sizeof(sensorFrameDuration), &sensorFrameDuration);
    }
#endif

    if (frame_settings.exists(ANDROID_SENSOR_SENSITIVITY)) {
        int32_t sensorSensitivity =
            frame_settings.find(ANDROID_SENSOR_SENSITIVITY).data.i32[0];
        if (sensorSensitivity <
                gCamCapability[mCameraId]->sensitivity_range.min_sensitivity)
            sensorSensitivity =
                gCamCapability[mCameraId]->sensitivity_range.min_sensitivity;
        if (sensorSensitivity >
                gCamCapability[mCameraId]->sensitivity_range.max_sensitivity)
            sensorSensitivity =
                gCamCapability[mCameraId]->sensitivity_range.max_sensitivity;
        rc = AddSetParmEntryToBatch(mParameters,
                CAM_INTF_META_SENSOR_SENSITIVITY,
                sizeof(sensorSensitivity), &sensorSensitivity);
    }

    if (frame_settings.exists(ANDROID_SHADING_MODE)) {
        int32_t shadingMode =
            frame_settings.find(ANDROID_SHADING_MODE).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_META_SHADING_MODE,
                sizeof(shadingMode), &shadingMode);
    }

    if (frame_settings.exists(ANDROID_SHADING_STRENGTH)) {
        uint8_t shadingStrength =
            frame_settings.find(ANDROID_SHADING_STRENGTH).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_META_SHADING_STRENGTH,
                sizeof(shadingStrength), &shadingStrength);
    }

    if (frame_settings.exists(ANDROID_STATISTICS_FACE_DETECT_MODE)) {
        uint8_t facedetectMode =
            frame_settings.find(ANDROID_STATISTICS_FACE_DETECT_MODE).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters,
                CAM_INTF_META_STATS_FACEDETECT_MODE,
                sizeof(facedetectMode), &facedetectMode);
    }

    if (frame_settings.exists(ANDROID_STATISTICS_HISTOGRAM_MODE)) {
        uint8_t histogramMode =
            frame_settings.find(ANDROID_STATISTICS_HISTOGRAM_MODE).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters,
                CAM_INTF_META_STATS_HISTOGRAM_MODE,
                sizeof(histogramMode), &histogramMode);
    }

    if (frame_settings.exists(ANDROID_STATISTICS_SHARPNESS_MAP_MODE)) {
        uint8_t sharpnessMapMode =
            frame_settings.find(ANDROID_STATISTICS_SHARPNESS_MAP_MODE).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters,
                CAM_INTF_META_STATS_SHARPNESS_MAP_MODE,
                sizeof(sharpnessMapMode), &sharpnessMapMode);
    }

    if (frame_settings.exists(ANDROID_TONEMAP_MODE)) {
        mTonemapMode = frame_settings.find(ANDROID_TONEMAP_MODE).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters,
                CAM_INTF_META_TONEMAP_MODE,
                sizeof(mTonemapMode), &mTonemapMode);
    }
    int point = 0;
    if (gCamCapability[mCameraId]->max_tone_map_curve_points == 0) {
        gCamCapability[mCameraId]->max_tone_map_curve_points = 64;
    }
    if (frame_settings.exists(ANDROID_TONEMAP_CURVE_BLUE)) {
        cam_tonemap_curve_t tonemapCurveBlue;
        tonemapCurveBlue.tonemap_points_cnt =
           gCamCapability[mCameraId]->max_tone_map_curve_points;
        for (int i = 0; i < tonemapCurveBlue.tonemap_points_cnt; i++) {
            for (int j = 0; j < 2; j++) {
               tonemapCurveBlue.tonemap_points[i][j] =
                  frame_settings.find(ANDROID_TONEMAP_CURVE_BLUE).data.f[point];
               point++;
            }
        }
        rc = AddSetParmEntryToBatch(mParameters,
                CAM_INTF_META_TONEMAP_CURVE_BLUE,
                sizeof(tonemapCurveBlue), &tonemapCurveBlue);
    }
    point = 0;
    if (frame_settings.exists(ANDROID_TONEMAP_CURVE_GREEN)) {
        cam_tonemap_curve_t tonemapCurveGreen;
        tonemapCurveGreen.tonemap_points_cnt =
           gCamCapability[mCameraId]->max_tone_map_curve_points;
        for (int i = 0; i < tonemapCurveGreen.tonemap_points_cnt; i++) {
            for (int j = 0; j < 2; j++) {
               tonemapCurveGreen.tonemap_points[i][j] =
                  frame_settings.find(ANDROID_TONEMAP_CURVE_GREEN).data.f[point];
               point++;
            }
        }
        rc = AddSetParmEntryToBatch(mParameters,
                CAM_INTF_META_TONEMAP_CURVE_GREEN,
                sizeof(tonemapCurveGreen), &tonemapCurveGreen);
    }
    point = 0;
    if (frame_settings.exists(ANDROID_TONEMAP_CURVE_RED)) {
        cam_tonemap_curve_t tonemapCurveRed;
        tonemapCurveRed.tonemap_points_cnt =
           gCamCapability[mCameraId]->max_tone_map_curve_points;
        for (int i = 0; i < tonemapCurveRed.tonemap_points_cnt; i++) {
            for (int j = 0; j < 2; j++) {
               tonemapCurveRed.tonemap_points[i][j] =
                  frame_settings.find(ANDROID_TONEMAP_CURVE_RED).data.f[point];
               point++;
            }
        }
        rc = AddSetParmEntryToBatch(mParameters,
                CAM_INTF_META_TONEMAP_CURVE_RED,
                sizeof(tonemapCurveRed), &tonemapCurveRed);
    }

    if (frame_settings.exists(ANDROID_CONTROL_CAPTURE_INTENT)) {
        uint8_t captureIntent =
            frame_settings.find(ANDROID_CONTROL_CAPTURE_INTENT).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_META_CAPTURE_INTENT,
                sizeof(captureIntent), &captureIntent);
    }

    if (frame_settings.exists(ANDROID_BLACK_LEVEL_LOCK)) {
        uint8_t blackLevelLock =
            frame_settings.find(ANDROID_BLACK_LEVEL_LOCK).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_META_BLACK_LEVEL_LOCK,
                sizeof(blackLevelLock), &blackLevelLock);
    }

    if (frame_settings.exists(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE)) {
        uint8_t lensShadingMapMode =
            frame_settings.find(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE).data.u8[0];
        rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_META_LENS_SHADING_MAP_MODE,
                sizeof(lensShadingMapMode), &lensShadingMapMode);
    }

    if (frame_settings.exists(ANDROID_CONTROL_AE_REGIONS)) {
        cam_area_t roi;
        bool reset = true;
        convertFromRegions(&roi, settings, ANDROID_CONTROL_AE_REGIONS);
        if (scalerCropSet) {
            reset = resetIfNeededROI(&roi, &scalerCropRegion);
        }
        if (reset) {
            rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_META_AEC_ROI,
                    sizeof(roi), &roi);
        }
    }

    if (frame_settings.exists(ANDROID_CONTROL_AF_REGIONS)) {
        cam_area_t roi;
        bool reset = true;
        convertFromRegions(&roi, settings, ANDROID_CONTROL_AF_REGIONS);
        if (scalerCropSet) {
            reset = resetIfNeededROI(&roi, &scalerCropRegion);
        }
        if (reset) {
            rc = AddSetParmEntryToBatch(mParameters, CAM_INTF_META_AF_ROI,
                    sizeof(roi), &roi);
        }
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : getJpegSettings
 *
 * DESCRIPTION: save the jpeg settings in the HAL
 *
 *
 * PARAMETERS :
 *   @settings  : frame settings information from framework
 *
 *
 * RETURN     : success: NO_ERROR
 *              failure:
 *==========================================================================*/
int QCamera3HardwareInterface::getJpegSettings
                                  (const camera_metadata_t *settings)
{
    if (mJpegSettings) {
        if (mJpegSettings->gps_timestamp) {
            free(mJpegSettings->gps_timestamp);
            mJpegSettings->gps_timestamp = NULL;
        }
        for (int i = 0; i < 3; i++) {
            free(mJpegSettings->gps_coordinates[i]);
            mJpegSettings->gps_coordinates[i] = NULL;
        }
        free(mJpegSettings);
        mJpegSettings = NULL;
    }
    mJpegSettings = (jpeg_settings_t*) malloc(sizeof(jpeg_settings_t));
    CameraMetadata jpeg_settings;
    jpeg_settings = settings;

    if (jpeg_settings.exists(ANDROID_JPEG_ORIENTATION)) {
        mJpegSettings->jpeg_orientation =
            jpeg_settings.find(ANDROID_JPEG_ORIENTATION).data.i32[0];
    } else {
        mJpegSettings->jpeg_orientation = 0;
    }
    if (jpeg_settings.exists(ANDROID_JPEG_QUALITY)) {
        mJpegSettings->jpeg_quality =
            jpeg_settings.find(ANDROID_JPEG_QUALITY).data.u8[0];
    } else {
        mJpegSettings->jpeg_quality = 85;
    }
    if (jpeg_settings.exists(ANDROID_JPEG_THUMBNAIL_SIZE)) {
        mJpegSettings->thumbnail_size.width =
            jpeg_settings.find(ANDROID_JPEG_THUMBNAIL_SIZE).data.i32[0];
        mJpegSettings->thumbnail_size.height =
            jpeg_settings.find(ANDROID_JPEG_THUMBNAIL_SIZE).data.i32[1];
    } else {
        mJpegSettings->thumbnail_size.width = 0;
        mJpegSettings->thumbnail_size.height = 0;
    }
    if (jpeg_settings.exists(ANDROID_JPEG_GPS_COORDINATES)) {
        for (int i = 0; i < 3; i++) {
            mJpegSettings->gps_coordinates[i] = (double*)malloc(sizeof(double*));
            *(mJpegSettings->gps_coordinates[i]) =
                jpeg_settings.find(ANDROID_JPEG_GPS_COORDINATES).data.d[i];
        }
    } else{
       for (int i = 0; i < 3; i++) {
            mJpegSettings->gps_coordinates[i] = NULL;
        }
    }

    if (jpeg_settings.exists(ANDROID_JPEG_GPS_TIMESTAMP)) {
        mJpegSettings->gps_timestamp = (int64_t*)malloc(sizeof(int64_t*));
        *(mJpegSettings->gps_timestamp) =
            jpeg_settings.find(ANDROID_JPEG_GPS_TIMESTAMP).data.i64[0];
    } else {
        mJpegSettings->gps_timestamp = NULL;
    }

    if (jpeg_settings.exists(ANDROID_JPEG_GPS_PROCESSING_METHOD)) {
        int len = jpeg_settings.find(ANDROID_JPEG_GPS_PROCESSING_METHOD).count;
        for (int i = 0; i < len; i++) {
            mJpegSettings->gps_processing_method[i] =
                jpeg_settings.find(ANDROID_JPEG_GPS_PROCESSING_METHOD).data.u8[i];
        }
        if (mJpegSettings->gps_processing_method[len-1] != '\0') {
            mJpegSettings->gps_processing_method[len] = '\0';
        }
    } else {
        mJpegSettings->gps_processing_method[0] = '\0';
    }

    mJpegSettings->sensor_sensitivity = mMetadataResponse.iso_speed;

    mJpegSettings->sensor_exposure_time = mMetadataResponse.exposure_time;

    if (jpeg_settings.exists(ANDROID_LENS_FOCAL_LENGTH)) {
        mJpegSettings->lens_focal_length =
            jpeg_settings.find(ANDROID_LENS_FOCAL_LENGTH).data.f[0];
    }
    if (jpeg_settings.exists(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION)) {
        mJpegSettings->exposure_compensation =
            jpeg_settings.find(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION).data.i32[0];
    }
    mJpegSettings->exposure_comp_step = gCamCapability[mCameraId]->exp_compensation_step;
    mJpegSettings->max_jpeg_size = calcMaxJpegSize();
    mJpegSettings->is_jpeg_format = true;
    mJpegSettings->min_required_pp_mask = gCamCapability[mCameraId]->min_required_pp_mask;
    mJpegSettings->f_number = gCamCapability[mCameraId]->apertures[0];

    if (jpeg_settings.exists(ANDROID_CONTROL_AWB_MODE)) {
        mJpegSettings->wb =
            jpeg_settings.find(ANDROID_CONTROL_AWB_MODE).data.u8[0];
    } else {
        mJpegSettings->wb = 0;
    }

    if (jpeg_settings.exists(ANDROID_FLASH_MODE)) {
        mJpegSettings->flash =
            jpeg_settings.find(ANDROID_FLASH_MODE).data.u8[0];
    } else {
        mJpegSettings->flash = 0;
    }


    return 0;
}

/*===========================================================================
 * FUNCTION   : captureResultCb
 *
 * DESCRIPTION: Callback handler for all channels (streams, as well as metadata)
 *
 * PARAMETERS :
 *   @frame  : frame information from mm-camera-interface
 *   @buffer : actual gralloc buffer to be returned to frameworks. NULL if metadata.
 *   @userdata: userdata
 *
 * RETURN     : NONE
 *==========================================================================*/
void QCamera3HardwareInterface::captureResultCb(mm_camera_super_buf_t *metadata,
                camera3_stream_buffer_t *buffer,
                uint32_t frame_number, void *userdata)
{
    QCamera3HardwareInterface *hw = (QCamera3HardwareInterface *)userdata;
    if (hw == NULL) {
        ALOGE("%s: Invalid hw %p", __func__, hw);
        return;
    }

    hw->captureResultCb(metadata, buffer, frame_number);
    return;
}


/*===========================================================================
 * FUNCTION   : initialize
 *
 * DESCRIPTION: Pass framework callback pointers to HAL
 *
 * PARAMETERS :
 *
 *
 * RETURN     : Success : 0
 *              Failure: -ENODEV
 *==========================================================================*/

int QCamera3HardwareInterface::initialize(const struct camera3_device *device,
                                  const camera3_callback_ops_t *callback_ops)
{
    ALOGV("%s: E", __func__);
    QCamera3HardwareInterface *hw =
        reinterpret_cast<QCamera3HardwareInterface *>(device->priv);
    if (!hw) {
        ALOGE("%s: NULL camera device", __func__);
        return -ENODEV;
    }

    int rc = hw->initialize(callback_ops);
    ALOGV("%s: X", __func__);
    return rc;
}

/*===========================================================================
 * FUNCTION   : configure_streams
 *
 * DESCRIPTION:
 *
 * PARAMETERS :
 *
 *
 * RETURN     : Success: 0
 *              Failure: -EINVAL (if stream configuration is invalid)
 *                       -ENODEV (fatal error)
 *==========================================================================*/

int QCamera3HardwareInterface::configure_streams(
        const struct camera3_device *device,
        camera3_stream_configuration_t *stream_list)
{
    ALOGV("%s: E", __func__);
    QCamera3HardwareInterface *hw =
        reinterpret_cast<QCamera3HardwareInterface *>(device->priv);
    if (!hw) {
        ALOGE("%s: NULL camera device", __func__);
        return -ENODEV;
    }
    int rc = hw->configureStreams(stream_list);
    ALOGV("%s: X", __func__);
    return rc;
}

/*===========================================================================
 * FUNCTION   : register_stream_buffers
 *
 * DESCRIPTION: Register stream buffers with the device
 *
 * PARAMETERS :
 *
 * RETURN     :
 *==========================================================================*/
int QCamera3HardwareInterface::register_stream_buffers(
        const struct camera3_device *device,
        const camera3_stream_buffer_set_t *buffer_set)
{
    ALOGV("%s: E", __func__);
    QCamera3HardwareInterface *hw =
        reinterpret_cast<QCamera3HardwareInterface *>(device->priv);
    if (!hw) {
        ALOGE("%s: NULL camera device", __func__);
        return -ENODEV;
    }
    int rc = hw->registerStreamBuffers(buffer_set);
    ALOGV("%s: X", __func__);
    return rc;
}

/*===========================================================================
 * FUNCTION   : construct_default_request_settings
 *
 * DESCRIPTION: Configure a settings buffer to meet the required use case
 *
 * PARAMETERS :
 *
 *
 * RETURN     : Success: Return valid metadata
 *              Failure: Return NULL
 *==========================================================================*/
const camera_metadata_t* QCamera3HardwareInterface::
    construct_default_request_settings(const struct camera3_device *device,
                                        int type)
{

    ALOGV("%s: E", __func__);
    camera_metadata_t* fwk_metadata = NULL;
    QCamera3HardwareInterface *hw =
        reinterpret_cast<QCamera3HardwareInterface *>(device->priv);
    if (!hw) {
        ALOGE("%s: NULL camera device", __func__);
        return NULL;
    }

    fwk_metadata = hw->translateCapabilityToMetadata(type);

    ALOGV("%s: X", __func__);
    return fwk_metadata;
}

/*===========================================================================
 * FUNCTION   : process_capture_request
 *
 * DESCRIPTION:
 *
 * PARAMETERS :
 *
 *
 * RETURN     :
 *==========================================================================*/
int QCamera3HardwareInterface::process_capture_request(
                    const struct camera3_device *device,
                    camera3_capture_request_t *request)
{
    ALOGV("%s: E", __func__);
    QCamera3HardwareInterface *hw =
        reinterpret_cast<QCamera3HardwareInterface *>(device->priv);
    if (!hw) {
        ALOGE("%s: NULL camera device", __func__);
        return -EINVAL;
    }

    int rc = hw->processCaptureRequest(request);
    ALOGV("%s: X", __func__);
    return rc;
}

/*===========================================================================
 * FUNCTION   : get_metadata_vendor_tag_ops
 *
 * DESCRIPTION:
 *
 * PARAMETERS :
 *
 *
 * RETURN     :
 *==========================================================================*/

void QCamera3HardwareInterface::get_metadata_vendor_tag_ops(
                const struct camera3_device *device,
                vendor_tag_query_ops_t* ops)
{
    ALOGV("%s: E", __func__);
    QCamera3HardwareInterface *hw =
        reinterpret_cast<QCamera3HardwareInterface *>(device->priv);
    if (!hw) {
        ALOGE("%s: NULL camera device", __func__);
        return;
    }

    hw->getMetadataVendorTagOps(ops);
    ALOGV("%s: X", __func__);
    return;
}

/*===========================================================================
 * FUNCTION   : dump
 *
 * DESCRIPTION:
 *
 * PARAMETERS :
 *
 *
 * RETURN     :
 *==========================================================================*/

void QCamera3HardwareInterface::dump(
                const struct camera3_device *device, int fd)
{
    ALOGV("%s: E", __func__);
    QCamera3HardwareInterface *hw =
        reinterpret_cast<QCamera3HardwareInterface *>(device->priv);
    if (!hw) {
        ALOGE("%s: NULL camera device", __func__);
        return;
    }

    hw->dump(fd);
    ALOGV("%s: X", __func__);
    return;
}

/*===========================================================================
 * FUNCTION   : flush
 *
 * DESCRIPTION:
 *
 * PARAMETERS :
 *
 *
 * RETURN     :
 *==========================================================================*/

int QCamera3HardwareInterface::flush(
                const struct camera3_device *device)
{
    int rc;
    ALOGV("%s: E", __func__);
    QCamera3HardwareInterface *hw =
        reinterpret_cast<QCamera3HardwareInterface *>(device->priv);
    if (!hw) {
        ALOGE("%s: NULL camera device", __func__);
        return -EINVAL;
    }

    rc = hw->flush();
    ALOGV("%s: X", __func__);
    return rc;
}

/*===========================================================================
 * FUNCTION   : close_camera_device
 *
 * DESCRIPTION:
 *
 * PARAMETERS :
 *
 *
 * RETURN     :
 *==========================================================================*/
int QCamera3HardwareInterface::close_camera_device(struct hw_device_t* device)
{
    ALOGV("%s: E", __func__);
    int ret = NO_ERROR;
    QCamera3HardwareInterface *hw =
        reinterpret_cast<QCamera3HardwareInterface *>(
            reinterpret_cast<camera3_device_t *>(device)->priv);
    if (!hw) {
        ALOGE("NULL camera device");
        return BAD_VALUE;
    }
    delete hw;

    pthread_mutex_lock(&mCameraSessionLock);
    mCameraSessionActive = 0;
    pthread_mutex_unlock(&mCameraSessionLock);
    ALOGV("%s: X", __func__);
    return ret;
}

/*===========================================================================
 * FUNCTION   : getWaveletDenoiseProcessPlate
 *
 * DESCRIPTION: query wavelet denoise process plate
 *
 * PARAMETERS : None
 *
 * RETURN     : WNR prcocess plate vlaue
 *==========================================================================*/
cam_denoise_process_type_t QCamera3HardwareInterface::getWaveletDenoiseProcessPlate()
{
    char prop[PROPERTY_VALUE_MAX];
    memset(prop, 0, sizeof(prop));
    property_get("persist.denoise.process.plates", prop, "0");
    int processPlate = atoi(prop);
    switch(processPlate) {
    case 0:
        return CAM_WAVELET_DENOISE_YCBCR_PLANE;
    case 1:
        return CAM_WAVELET_DENOISE_CBCR_ONLY;
    case 2:
        return CAM_WAVELET_DENOISE_STREAMLINE_YCBCR;
    case 3:
        return CAM_WAVELET_DENOISE_STREAMLINED_CBCR;
    default:
        return CAM_WAVELET_DENOISE_STREAMLINE_YCBCR;
    }
}

/*===========================================================================
 * FUNCTION   : needRotationReprocess
 *
 * DESCRIPTION: if rotation needs to be done by reprocess in pp
 *
 * PARAMETERS : none
 *
 * RETURN     : true: needed
 *              false: no need
 *==========================================================================*/
bool QCamera3HardwareInterface::needRotationReprocess()
{

    if (!mJpegSettings->is_jpeg_format) {
        // RAW image, no need to reprocess
        return false;
    }

    if ((gCamCapability[mCameraId]->qcom_supported_feature_mask & CAM_QCOM_FEATURE_ROTATION) > 0 &&
        mJpegSettings->jpeg_orientation > 0) {
        // current rotation is not zero, and pp has the capability to process rotation
        ALOGD("%s: need do reprocess for rotation", __func__);
        return true;
    }

    return false;
}

/*===========================================================================
 * FUNCTION   : needReprocess
 *
 * DESCRIPTION: if reprocess in needed
 *
 * PARAMETERS : none
 *
 * RETURN     : true: needed
 *              false: no need
 *==========================================================================*/
bool QCamera3HardwareInterface::needReprocess()
{
    if (!mJpegSettings->is_jpeg_format) {
        // RAW image, no need to reprocess
        return false;
    }

    if ((mJpegSettings->min_required_pp_mask > 0) ||
         isWNREnabled()) {
        // TODO: add for ZSL HDR later
        // pp module has min requirement for zsl reprocess, or WNR in ZSL mode
        ALOGD("%s: need do reprocess for ZSL WNR or min PP reprocess", __func__);
        return true;
    }
    return needRotationReprocess();
}

/*===========================================================================
 * FUNCTION   : addOnlineReprocChannel
 *
 * DESCRIPTION: add a online reprocess channel that will do reprocess on frames
 *              coming from input channel
 *
 * PARAMETERS :
 *   @pInputChannel : ptr to input channel whose frames will be post-processed
 *
 * RETURN     : Ptr to the newly created channel obj. NULL if failed.
 *==========================================================================*/
QCamera3ReprocessChannel *QCamera3HardwareInterface::addOnlineReprocChannel(
              QCamera3Channel *pInputChannel, QCamera3PicChannel *picChHandle)
{
    int32_t rc = NO_ERROR;
    QCamera3ReprocessChannel *pChannel = NULL;
    if (pInputChannel == NULL) {
        ALOGE("%s: input channel obj is NULL", __func__);
        return NULL;
    }

    pChannel = new QCamera3ReprocessChannel(mCameraHandle->camera_handle,
            mCameraHandle->ops, NULL, pInputChannel->mPaddingInfo, this, picChHandle);
    if (NULL == pChannel) {
        ALOGE("%s: no mem for reprocess channel", __func__);
        return NULL;
    }

    // Capture channel, only need snapshot and postview streams start together
    mm_camera_channel_attr_t attr;
    memset(&attr, 0, sizeof(mm_camera_channel_attr_t));
    attr.notify_mode = MM_CAMERA_SUPER_BUF_NOTIFY_CONTINUOUS;
    attr.max_unmatched_frames = getMaxUnmatchedFramesInQueue();
    rc = pChannel->initialize();
    if (rc != NO_ERROR) {
        ALOGE("%s: init reprocess channel failed, ret = %d", __func__, rc);
        delete pChannel;
        return NULL;
    }

    // pp feature config
    cam_pp_feature_config_t pp_config;
    memset(&pp_config, 0, sizeof(cam_pp_feature_config_t));
    if (gCamCapability[mCameraId]->min_required_pp_mask & CAM_QCOM_FEATURE_SHARPNESS) {
        pp_config.feature_mask |= CAM_QCOM_FEATURE_SHARPNESS;
        pp_config.sharpness = 10;
    }

    if (isWNREnabled()) {
        pp_config.feature_mask |= CAM_QCOM_FEATURE_DENOISE2D;
        pp_config.denoise2d.denoise_enable = 1;
        pp_config.denoise2d.process_plates = getWaveletDenoiseProcessPlate();
    }
    if (needRotationReprocess()) {
        pp_config.feature_mask |= CAM_QCOM_FEATURE_ROTATION;
        int rotation = mJpegSettings->jpeg_orientation;
        if (rotation == 0) {
            pp_config.rotation = ROTATE_0;
        } else if (rotation == 90) {
            pp_config.rotation = ROTATE_90;
        } else if (rotation == 180) {
            pp_config.rotation = ROTATE_180;
        } else if (rotation == 270) {
            pp_config.rotation = ROTATE_270;
        }
    }

   rc = pChannel->addReprocStreamsFromSource(pp_config,
                                             pInputChannel,
                                             mMetadataChannel);

    if (rc != NO_ERROR) {
        delete pChannel;
        return NULL;
    }
    return pChannel;
}

int QCamera3HardwareInterface::getMaxUnmatchedFramesInQueue()
{
    return gCamCapability[mCameraId]->min_num_pp_bufs;
}

bool QCamera3HardwareInterface::isWNREnabled() {
    return gCamCapability[mCameraId]->isWnrSupported;
}

}; //end namespace qcamera
