/*
 * media_service.cc
 * Copyright (C) 2023 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "media/media_service.h"

#include <memory>
#include <utility>

#include "api/video/encoded_image.h"
#include "api/video/video_sink_interface.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "api/video_codecs/fake_video_encoder_factory.h"
#include "api/video_codecs/video_encoder_config.h"
#include "app/app_config.h"
#include "base/checks.h"
#include "base/errors.h"
#include "base/logging.h"
#include "common/looper.h"
#include "common/message.h"
#include "media/hybird_worker.h"
#include "media/video/file_sink.h"
#include "media/video/v4l2_video_source.h"
#include "media/video/video_sink_wrapper.h"

namespace avp {

MediaService::MediaService(AppConfig appConfig, std::shared_ptr<Message> notify)
    : app_config_(appConfig),
      notify_(std::move(notify)),
      looper_(new Looper()),
      media_info_(std::make_shared<Message>()),
      max_stream_id_(0),
      video_source_(nullptr),
      // tmp_factory_(std::make_unique<FakeVideoEncoderFactory>()),
      tmp_factory_(CreateBuiltinVideoEncoderFactory()),
      video_encoder_factory_(nullptr),
      video_capturer_(nullptr),
      file_sink_(std::make_shared<FileSink<EncodedImage>>("file1.frame")),
      file_sink2_(std::make_shared<FileSink<EncodedImage>>("file2.frame")) {
  looper_->setName("MediaService");
}

MediaService::~MediaService() {}

status_t MediaService::Init() {
  looper_->start();
  looper_->registerHandler(shared_from_this());

  // TODO(youfa), develop usage, remove later
  video_encoder_factory_ = tmp_factory_.get();
  if (video_encoder_factory_) {
    media_workers_.push_back(
        std::make_unique<HybirdWorker>(video_encoder_factory_));
  }

  // media_info_->setString("v4l2-dev", app_config_.v4l2_device);
  // video_capturer_ = std::make_shared<VideoCapturer>(media_info_);
  // video_source_ = V4L2VideoSource::Create(media_info_);

  // video_capturer_->SetVideoSource(video_source_.get(), VideoSinkWants());

  // video_capturer_->AddOrUpdateSink(row_file_sink_.get(), VideoSinkWants());

  // TODO(Youfa) for test
  // int32_t id = GenerateStreamId();
  // auto msg = std::make_shared<Message>(kWhatAddVideoSource,
  // shared_from_this()); msg->setObject("video_source", video_capturer_);
  // msg->setInt32("stream_id", id);
  // msg->setInt32("codec_format",
  //              static_cast<int32_t>(CodecId::AV_CODEC_ID_H264));
  // msg->setInt32("min_kbps", 300);
  // msg->setInt32("max_kbps", 1000);
  // msg->post();

  // msg = std::make_shared<Message>(kWhatAddEncodedVideoSink,
  // shared_from_this()); msg->setObject("encoded_video_sink", file_sink_);
  // msg->setInt32("stream_id", id);
  // msg->post();

  // msg = std::make_shared<Message>(kWhatAddEncodedVideoSink,
  // shared_from_this()); msg->setObject("encoded_video_sink", file_sink2_);
  // msg->setInt32("stream_id", id);
  // msg->post();

  return OK;
}

status_t MediaService::Start() {
  return OK;
}

status_t MediaService::Stop() {
  return OK;
}

void MediaService::AddVideoSource(
    std::shared_ptr<VideoSourceInterface<std::shared_ptr<VideoFrame>>>&
        video_source,
    int32_t stream_id,
    CodecId codec_id,
    int32_t min_bitrate,
    int32_t max_bitrate) {
  // TODO(youfa) capturer lock
  // video_capturers_.push_back({std::make_unique<VideoCapturer>(nullptr), id});

  auto msg = std::make_shared<Message>(kWhatAddVideoSource, shared_from_this());
  msg->setObject("video_source",
                 std::dynamic_pointer_cast<MessageObject>(video_source));
  msg->setInt32("stream_id", stream_id);
  msg->setInt32("codec_format", static_cast<int32_t>(codec_id));
  msg->setInt32("min_kbps", min_bitrate);
  msg->setInt32("max_kbps", max_bitrate);
  msg->post();
}

void MediaService::AddVideoSink(
    const std::shared_ptr<VideoSinkInterface<EncodedImage>>& video_sink,
    int32_t stream_id) {
  auto msg =
      std::make_shared<Message>(kWhatAddEncodedVideoSink, shared_from_this());
  msg->setObject("encoded_video_sink",
                 std::dynamic_pointer_cast<MessageObject>(
                     VideoSinkWrapper::Create(video_sink)));
  msg->setInt32("stream_id", stream_id);
  msg->post();
}

void MediaService::RequesteKeyFrame() {
  auto msg =
      std::make_shared<Message>(kWhatRequestKeyFrame, shared_from_this());
  msg->post();
}

uint32_t MediaService::GenerateStreamId() {
  return ++max_stream_id_;
}

void MediaService::onMessageReceived(const std::shared_ptr<Message>& message) {
  using VideoSource = VideoSourceInterface<std::shared_ptr<VideoFrame>>;
  using EncodedVideoSink = VideoSinkInterface<EncodedImage>;
  switch (message->what()) {
    case kWhatStart: {
      break;
    }
    case kWhatStop: {
      break;
    }

    case kWhatAddVideoSource: {
      std::shared_ptr<MessageObject> obj;
      CHECK(message->findObject("video_source", obj));
      std::shared_ptr<VideoSource> video_source =
          std::dynamic_pointer_cast<VideoSource>(obj);
      DCHECK(video_source != nullptr);

      int32_t id;
      CHECK(message->findInt32("stream_id", &id));

      VideoEncoderConfig encoder_config;

      int32_t codec_id;
      CHECK(message->findInt32("codec_format", &codec_id));
      encoder_config.codec_id = static_cast<CodecId>(codec_id);

      int32_t min_bitrate;
      CHECK(message->findInt32("min_kbps", &min_bitrate));
      encoder_config.min_bitrate_kbps = min_bitrate;

      int32_t max_bitrate;
      CHECK(message->findInt32("max_kbps", &max_bitrate));
      encoder_config.max_bitrate_kbps = max_bitrate;

      // add source to each media worker
      for (auto& worker : media_workers_) {
        auto config = encoder_config.Copy();
        worker->AddVideoSource(video_source, id, config);
      }

      AddVideoSink(std::dynamic_pointer_cast<VideoSinkInterface<EncodedImage>>(
                       file_sink_),
                   id);

      break;
    }

    case kWhatRemoveVideoSource: {
      std::shared_ptr<MessageObject> obj;
      CHECK(message->findObject("video_source", obj));
      std::shared_ptr<VideoSource> video_source =
          std::dynamic_pointer_cast<VideoSource>(obj);
      DCHECK(video_source != nullptr);

      int32_t id;
      CHECK(message->findInt32("stream_id", &id));

      // add source to each media worker
      for (auto& worker : media_workers_) {
        worker->RemoveVideoSource(video_source, id);
      }
      break;
    }

    case kWhatAddEncodedVideoSink: {
      std::shared_ptr<MessageObject> obj;
      CHECK(message->findObject("encoded_video_sink", obj));
      std::shared_ptr<EncodedVideoSink> encoded_video_sink =
          std::dynamic_pointer_cast<EncodedVideoSink>(obj);
      DCHECK(encoded_video_sink != nullptr);

      int32_t id;
      CHECK(message->findInt32("stream_id", &id));

      // add sink to each media worker
      for (auto& worker : media_workers_) {
        worker->AddEncodedVideoSink(encoded_video_sink, id);
      }
      break;
    }
    case kWhatRemoveEncodedVideoSink: {
      std::shared_ptr<MessageObject> obj;
      CHECK(message->findObject("encoded_video_sink", obj));
      std::shared_ptr<EncodedVideoSink> encoded_video_sink =
          std::dynamic_pointer_cast<EncodedVideoSink>(obj);
      DCHECK(encoded_video_sink != nullptr);

      int32_t id;
      CHECK(message->findInt32("stream_id", &id));

      for (auto& worker : media_workers_) {
        worker->RemoveEncodedVideoSink(encoded_video_sink, id);
      }

      break;
    }

    case kWhatEnableMotionDetector: {
      break;
    }

    case kWhatRequestKeyFrame: {
      for (auto it = media_workers_.begin(); it != media_workers_.end(); ++it) {
        (*it)->RequestKeyFrame();
      }
      break;
    }

    default: {
      break;
    }
  }
}

}  // namespace avp
