// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/audio/audio_service.h"

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/strings/string_number_conversions.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace extensions {

using api::audio::OutputDeviceInfo;
using api::audio::InputDeviceInfo;

class AudioServiceImpl : public AudioService {
 public:
  AudioServiceImpl();
  virtual ~AudioServiceImpl();

  // Called by listeners to this service to add/remove themselves as observers.
  virtual void AddObserver(AudioService::Observer* observer) OVERRIDE;
  virtual void RemoveObserver(AudioService::Observer* observer) OVERRIDE;

  // Start to query audio device information.
  virtual void StartGetInfo(const GetInfoCallback& callback) OVERRIDE;
  virtual void SetActiveDevices(const DeviceIdList& device_list) OVERRIDE;
  virtual bool SetDeviceProperties(const std::string& device_id,
                                   bool muted,
                                   int volume,
                                   int gain) OVERRIDE;

  // List of observers.
  ObserverList<AudioService::Observer> observer_list_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate the weak pointers before any other members are destroyed.
  base::WeakPtrFactory<AudioServiceImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(AudioServiceImpl);
};

AudioServiceImpl::AudioServiceImpl() : weak_ptr_factory_(this) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
}

AudioServiceImpl::~AudioServiceImpl() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
}

void AudioServiceImpl::AddObserver(AudioService::Observer* observer) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  observer_list_.AddObserver(observer);
}

void AudioServiceImpl::RemoveObserver(AudioService::Observer* observer) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  observer_list_.RemoveObserver(observer);
}

void AudioServiceImpl::StartGetInfo(const GetInfoCallback& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!callback.is_null())
    callback.Run(OutputInfo(), InputInfo(), false);
}

void AudioServiceImpl::SetActiveDevices(const DeviceIdList& device_list) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
}

bool AudioServiceImpl::SetDeviceProperties(const std::string& device_id,
                                           bool muted,
                                           int volume,
                                           int gain) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return false;
}

AudioService* AudioService::CreateInstance() {
  return new AudioServiceImpl;
}

}  // namespace extensions
