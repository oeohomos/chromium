// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// IPC messages for the media streaming.
// Multiply-included message file, hence no include guard.

#include <string>

#include "content/common/content_export.h"
#include "content/common/media/media_stream_options.h"
#include "ipc/ipc_message_macros.h"

#undef IPC_MESSAGE_EXPORT
#define IPC_MESSAGE_EXPORT CONTENT_EXPORT
#define IPC_MESSAGE_START MediaStreamMsgStart

IPC_ENUM_TRAITS(media_stream::MediaStreamType)

IPC_STRUCT_TRAITS_BEGIN(media_stream::StreamOptions)
  IPC_STRUCT_TRAITS_MEMBER(audio)
  IPC_STRUCT_TRAITS_MEMBER(video)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(media_stream::StreamDeviceInfo)
  IPC_STRUCT_TRAITS_MEMBER(stream_type)
  IPC_STRUCT_TRAITS_MEMBER(name)
  IPC_STRUCT_TRAITS_MEMBER(device_id)
  IPC_STRUCT_TRAITS_MEMBER(in_use)
  IPC_STRUCT_TRAITS_MEMBER(session_id)
IPC_STRUCT_TRAITS_END()

// Message sent from the browser to the renderer

// The browser has generated a stream successfully.
IPC_MESSAGE_ROUTED4(MediaStreamMsg_StreamGenerated,
                    int /* request id */,
                    std::string /* label */,
                    media_stream::StreamDeviceInfoArray /* audio_device_list */,
                    media_stream::StreamDeviceInfoArray /* video_device_list */)

// The browser has failed to generate a stream.
IPC_MESSAGE_ROUTED1(MediaStreamMsg_StreamGenerationFailed,
                    int /* request id */)

// Report of a failure of a video device.
IPC_MESSAGE_ROUTED2(MediaStreamHostMsg_VideoDeviceFailed,
                    std::string /* label */,
                    int /* index */)

// Report of a failure of an audio device.
IPC_MESSAGE_ROUTED2(MediaStreamHostMsg_AudioDeviceFailed,
                    std::string /* label */,
                    int /* index */)

// The browser has enumerated devices successfully.
IPC_MESSAGE_ROUTED2(MediaStreamMsg_DevicesEnumerated,
                    int /* request id */,
                    media_stream::StreamDeviceInfoArray /* device_list */)

// The browser has failed to enumerate devices.
IPC_MESSAGE_ROUTED1(MediaStreamMsg_DevicesEnumerationFailed,
                    int /* request id */)

// TODO(wjia): should DeviceOpen* messages be merged with
// StreamGenerat* ones?
// The browser has opened a device successfully.
IPC_MESSAGE_ROUTED3(MediaStreamMsg_DeviceOpened,
                    int /* request id */,
                    std::string /* label */,
                    media_stream::StreamDeviceInfo /* the device */)

// The browser has failed to open a device.
IPC_MESSAGE_ROUTED1(MediaStreamMsg_DeviceOpenFailed,
                    int /* request id */)

// Messages sent from the renderer to the browser.

// Request a new media stream.
IPC_MESSAGE_CONTROL4(MediaStreamHostMsg_GenerateStream,
                     int /* render view id */,
                     int /* request id */,
                     media_stream::StreamOptions /* options */,
                     std::string /* security origin */)

// Request to cancel the request for a new  media stream.
IPC_MESSAGE_CONTROL2(MediaStreamHostMsg_CancelGenerateStream,
                     int /* render view id */,
                     int /* request id */)

// Request to stop streaming from the media stream.
IPC_MESSAGE_CONTROL2(MediaStreamHostMsg_StopGeneratedStream,
                     int /* render view id */,
                     std::string /* label */)

// Request to enumerate devices.
IPC_MESSAGE_CONTROL4(MediaStreamHostMsg_EnumerateDevices,
                     int /* render view id */,
                     int /* request id */,
                     media_stream::MediaStreamType /* type */,
                     std::string /* security origin */)

// Request to open the device.
IPC_MESSAGE_CONTROL5(MediaStreamHostMsg_OpenDevice,
                     int /* render view id */,
                     int /* request id */,
                     std::string /* device_id */,
                     media_stream::MediaStreamType /* type */,
                     std::string /* security origin */)
