// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_NETWORK_STATE_NOTIFIER_H_
#define CHROME_BROWSER_CHROMEOS_NETWORK_STATE_NOTIFIER_H_

#include "chrome/browser/chromeos/cros/network_library.h"

#include "base/singleton.h"
#include "base/task.h"

namespace chromeos {

// NetworkStateDetails contains the information about
// network status.
class NetworkStateDetails {
 public:
  enum State {
    UNKNOWN = 0,
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
  };

  State state() const {
    return state_;
  }

 private:
  friend class NetworkStateNotifier;

  explicit NetworkStateDetails(State state)
      : state_(state) {
  }

  State state_;

  DISALLOW_COPY_AND_ASSIGN(NetworkStateDetails);
};

// NetworkStateNotifier sends notification when network state has
// chagned. Notificatio is sent in UI thread.
// TODO(oshima): port this to other platform. merge with
// NetworkChangeNotifier if possible.
class NetworkStateNotifier : public NetworkLibrary::Observer {
 public:
  // Returns the singleton instance of the network state notifier;
  static NetworkStateNotifier* Get();

  // NetworkLibrary::Observer implementation.
  virtual void NetworkChanged(NetworkLibrary* cros);
  virtual void NetworkTraffic(NetworkLibrary* cros, int traffic_type) {}

  // Returns true if the network is connected.
  static bool is_connected() {
    return Get()->state_ == NetworkStateDetails::CONNECTED;
  }

 private:
  friend struct DefaultSingletonTraits<NetworkStateNotifier>;

  // Retrieve the current state from libcros.
  static NetworkStateDetails::State RetrieveState();

  NetworkStateNotifier();
  virtual ~NetworkStateNotifier() {}

  // Update the current state and sends notification to observers.
  // This should be invoked in UI thread.
  void UpdateNetworkState(NetworkStateDetails::State new_state);

  // The current network state.
  NetworkStateDetails::State state_;

  ScopedRunnableMethodFactory<NetworkStateNotifier> task_factory_;

  DISALLOW_COPY_AND_ASSIGN(NetworkStateNotifier);
};

}  // chromeos

#endif  // CHROME_BROWSER_CHROMEOS_NETWORK_STATE_NOTIFIER_H_
