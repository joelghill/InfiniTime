#include "components/ble/AppleMediaServiceClient.h"
#include "components/ble/NimbleController.h"
#include <cstring>
#include <cstdlib>
#include <nrf_log.h>
#include <FreeRTOS.h>
#include <task.h>

using namespace Pinetime::Controllers;

namespace {
  int OnAMSDiscoveryEventCallback(uint16_t conn_handle, const struct ble_gatt_error* error, const struct ble_gatt_svc* service, void* arg) {
    auto client = static_cast<AppleMediaServiceClient*>(arg);
    return client->OnDiscoveryEvent(conn_handle, error, service);
  }

  int OnAMSCharacteristicDiscoveredCallback(uint16_t conn_handle,
                                            const struct ble_gatt_error* error,
                                            const struct ble_gatt_chr* chr,
                                            void* arg) {
    auto client = static_cast<AppleMediaServiceClient*>(arg);
    return client->OnCharacteristicsDiscoveryEvent(conn_handle, error, chr);
  }

  int OnAMSDescriptorDiscoveryEventCallback(uint16_t conn_handle,
                                            const struct ble_gatt_error* error,
                                            uint16_t chr_val_handle,
                                            const struct ble_gatt_dsc* dsc,
                                            void* arg) {
    auto client = static_cast<AppleMediaServiceClient*>(arg);
    return client->OnDescriptorDiscoveryEventCallback(conn_handle, error, chr_val_handle, dsc);
  }

  int OnAMSSubscribeCallback(uint16_t conn_handle, const struct ble_gatt_error* error, struct ble_gatt_attr* attr, void* arg) {
    auto client = static_cast<AppleMediaServiceClient*>(arg);
    return client->OnSubscribeCallback(conn_handle, error, attr);
  }

  int OnAMSEntityUpdateWriteCallback(uint16_t conn_handle,
                                     const struct ble_gatt_error* error,
                                     struct ble_gatt_attr* attr,
                                     void* arg) {
    auto client = static_cast<AppleMediaServiceClient*>(arg);
    return client->OnEntityUpdateWriteCallback(conn_handle, error, attr);
  }
}

AppleMediaServiceClient::AppleMediaServiceClient(NimbleController& nimble) : nimble {nimble} {
}

void AppleMediaServiceClient::Discover(uint16_t connectionHandle, std::function<void(uint16_t)> onServiceDiscovered) {
  NRF_LOG_INFO("[AMS] Starting discovery");
  this->onServiceDiscovered = onServiceDiscovered;
  ble_gattc_disc_svc_by_uuid(connectionHandle, &amsUuid.u, OnAMSDiscoveryEventCallback, this);
}

bool AppleMediaServiceClient::OnDiscoveryEvent(uint16_t connectionHandle, const ble_gatt_error* error, const ble_gatt_svc* service) {
  if (service == nullptr && error->status == BLE_HS_EDONE) {
    if (isDiscovered) {
      NRF_LOG_INFO("[AMS] Service found, starting characteristic discovery");
      ble_gattc_disc_all_chrs(connectionHandle, amsStartHandle, amsEndHandle, OnAMSCharacteristicDiscoveredCallback, this);
    } else {
      NRF_LOG_INFO("[AMS] Service not found");
      onServiceDiscovered(connectionHandle);
    }
    return true;
  }

  if (service != nullptr && ble_uuid_cmp(&amsUuid.u, &service->uuid.u) == 0) {
    NRF_LOG_INFO("[AMS] Service discovered: 0x%x - 0x%x", service->start_handle, service->end_handle);
    amsStartHandle = service->start_handle;
    amsEndHandle = service->end_handle;
    isDiscovered = true;
  }
  return false;
}

int AppleMediaServiceClient::OnCharacteristicsDiscoveryEvent(uint16_t connectionHandle,
                                                             const ble_gatt_error* error,
                                                             const ble_gatt_chr* characteristic) {
  if (error->status != 0 && error->status != BLE_HS_EDONE) {
    NRF_LOG_INFO("[AMS] Characteristic discovery error: %d", error->status);
    onServiceDiscovered(connectionHandle);
    return 0;
  }

  if (characteristic == nullptr && error->status == BLE_HS_EDONE) {
    NRF_LOG_INFO("[AMS] Characteristic discovery complete");

    if (isRemoteCommandCharDiscovered) {
      ble_gattc_disc_all_dscs(connectionHandle, remoteCommandHandle, amsEndHandle, OnAMSDescriptorDiscoveryEventCallback, this);
    }
    if (isEntityUpdateCharDiscovered) {
      ble_gattc_disc_all_dscs(connectionHandle, entityUpdateHandle, amsEndHandle, OnAMSDescriptorDiscoveryEventCallback, this);
    }

    if (!isRemoteCommandCharDiscovered && !isEntityUpdateCharDiscovered) {
      onServiceDiscovered(connectionHandle);
    }
  } else if (characteristic != nullptr) {
    if (ble_uuid_cmp(&remoteCommandChar.u, &characteristic->uuid.u) == 0) {
      NRF_LOG_INFO("[AMS] Remote Command characteristic discovered");
      remoteCommandHandle = characteristic->val_handle;
      isRemoteCommandCharDiscovered = true;
    } else if (ble_uuid_cmp(&entityUpdateChar.u, &characteristic->uuid.u) == 0) {
      NRF_LOG_INFO("[AMS] Entity Update characteristic discovered");
      entityUpdateHandle = characteristic->val_handle;
      isEntityUpdateCharDiscovered = true;
    } else if (ble_uuid_cmp(&entityAttributeChar.u, &characteristic->uuid.u) == 0) {
      NRF_LOG_INFO("[AMS] Entity Attribute characteristic discovered");
      entityAttributeHandle = characteristic->val_handle;
      isEntityAttributeCharDiscovered = true;
    }
  }
  return 0;
}

int AppleMediaServiceClient::OnDescriptorDiscoveryEventCallback(uint16_t connectionHandle,
                                                                const ble_gatt_error* error,
                                                                uint16_t characteristicValueHandle,
                                                                const ble_gatt_dsc* descriptor) {
  if (error->status == 0) {
    if (characteristicValueHandle == remoteCommandHandle && ble_uuid_cmp(&remoteCommandChar.u, &descriptor->uuid.u)) {
      if (remoteCommandDescriptorHandle == 0) {
        NRF_LOG_INFO("[AMS] Remote Command descriptor discovered: %d", descriptor->handle);
        remoteCommandDescriptorHandle = descriptor->handle;
        isRemoteCommandDescriptorFound = true;
        uint8_t value[2] {1, 0};
        ble_gattc_write_flat(connectionHandle, remoteCommandDescriptorHandle, value, sizeof(value), OnAMSSubscribeCallback, this);
      }
    } else if (characteristicValueHandle == entityUpdateHandle && ble_uuid_cmp(&entityUpdateChar.u, &descriptor->uuid.u)) {
      if (entityUpdateDescriptorHandle == 0) {
        NRF_LOG_INFO("[AMS] Entity Update descriptor discovered: %d", descriptor->handle);
        entityUpdateDescriptorHandle = descriptor->handle;
        isEntityUpdateDescriptorFound = true;
        uint8_t value[2] {1, 0};
        ble_gattc_write_flat(connectionHandle, entityUpdateDescriptorHandle, value, sizeof(value), OnAMSSubscribeCallback, this);
      }
    }
  } else {
    if (error->status != BLE_HS_EDONE) {
      NRF_LOG_INFO("[AMS] Descriptor discovery error: %d", error->status);
    }
    MaybeFinishDiscovery(connectionHandle);
  }
  return 0;
}

int AppleMediaServiceClient::OnSubscribeCallback(uint16_t connectionHandle,
                                                  const ble_gatt_error* error,
                                                  ble_gatt_attr* /*attribute*/) {
  if (error->status == 0) {
    NRF_LOG_INFO("[AMS] Subscribe OK");
    if (remoteCommandDescriptorHandle != 0 && entityUpdateDescriptorHandle != 0) {
      subscriptionsDone = true;
    }
  } else {
    NRF_LOG_INFO("[AMS] Subscribe error: %d", error->status);
  }
  MaybeFinishDiscovery(connectionHandle);
  return 0;
}

void AppleMediaServiceClient::MaybeFinishDiscovery(uint16_t connectionHandle) {
  if (isRemoteCommandCharDiscovered && isEntityUpdateCharDiscovered && isRemoteCommandDescriptorFound && isEntityUpdateDescriptorFound &&
      subscriptionsDone) {
    if (!entityUpdatesRegistered) {
      RegisterEntityUpdates(connectionHandle);
    }
    onServiceDiscovered(connectionHandle);
  }
}

void AppleMediaServiceClient::RegisterEntityUpdates(uint16_t connectionHandle) {
  entityUpdatesRegistered = true;

  // Register for Track entity updates: Artist, Album, Title, Duration
  uint8_t trackRequest[] = {
    static_cast<uint8_t>(EntityId::Track),
    static_cast<uint8_t>(TrackAttributeId::Artist),
    static_cast<uint8_t>(TrackAttributeId::Album),
    static_cast<uint8_t>(TrackAttributeId::Title),
    static_cast<uint8_t>(TrackAttributeId::Duration),
  };
  ble_gattc_write_flat(connectionHandle, entityUpdateHandle, trackRequest, sizeof(trackRequest), OnAMSEntityUpdateWriteCallback, this);

  // Register for Player entity updates: PlaybackInfo, Volume
  uint8_t playerRequest[] = {
    static_cast<uint8_t>(EntityId::Player),
    static_cast<uint8_t>(PlayerAttributeId::PlaybackInfo),
    static_cast<uint8_t>(PlayerAttributeId::Volume),
  };
  ble_gattc_write_flat(connectionHandle, entityUpdateHandle, playerRequest, sizeof(playerRequest), OnAMSEntityUpdateWriteCallback, this);

  // Register for Queue entity updates: ShuffleMode, RepeatMode
  uint8_t queueRequest[] = {
    static_cast<uint8_t>(EntityId::Queue),
    static_cast<uint8_t>(QueueAttributeId::ShuffleMode),
    static_cast<uint8_t>(QueueAttributeId::RepeatMode),
  };
  ble_gattc_write_flat(connectionHandle, entityUpdateHandle, queueRequest, sizeof(queueRequest), OnAMSEntityUpdateWriteCallback, this);
}

int AppleMediaServiceClient::OnEntityUpdateWriteCallback(uint16_t /*connectionHandle*/,
                                                         const ble_gatt_error* error,
                                                         ble_gatt_attr* /*attribute*/) {
  if (error->status == 0) {
    NRF_LOG_INFO("[AMS] Entity Update registration OK");
  } else {
    NRF_LOG_INFO("[AMS] Entity Update registration error: %d", error->status);
  }
  return 0;
}

void AppleMediaServiceClient::OnNotification(ble_gap_event* event) {
  if (event->notify_rx.attr_handle == entityUpdateHandle) {
    uint16_t length = OS_MBUF_PKTLEN(event->notify_rx.om);
    if (length < 3) {
      return;
    }
    ParseEntityUpdate(event->notify_rx.om, length);
  }
  // Remote Command notifications contain the list of supported commands.
  // We don't currently track supported commands, so nothing to do here.
}

void AppleMediaServiceClient::ParseEntityUpdate(os_mbuf* om, uint16_t length) {
  uint8_t entityId;
  uint8_t attributeId;
  uint8_t flags;

  os_mbuf_copydata(om, 0, 1, &entityId);
  os_mbuf_copydata(om, 1, 1, &attributeId);
  os_mbuf_copydata(om, 2, 1, &flags);

  // Value starts at byte 3
  uint16_t valueLength = length - 3;

  // Extract value string
  constexpr uint8_t maxValueSize = 64;
  uint16_t copyLength = valueLength < maxValueSize ? valueLength : maxValueSize;
  char valueBuf[maxValueSize + 1];
  os_mbuf_copydata(om, 3, copyLength, valueBuf);
  valueBuf[copyLength] = '\0';

  if (entityId == static_cast<uint8_t>(EntityId::Track)) {
    if (attributeId == static_cast<uint8_t>(TrackAttributeId::Artist)) {
      artistName = valueBuf;
      NRF_LOG_INFO("[AMS] Artist: %s", artistName.c_str());
    } else if (attributeId == static_cast<uint8_t>(TrackAttributeId::Album)) {
      albumName = valueBuf;
      NRF_LOG_INFO("[AMS] Album: %s", albumName.c_str());
    } else if (attributeId == static_cast<uint8_t>(TrackAttributeId::Title)) {
      trackName = valueBuf;
      NRF_LOG_INFO("[AMS] Title: %s", trackName.c_str());
    } else if (attributeId == static_cast<uint8_t>(TrackAttributeId::Duration)) {
      trackLength = static_cast<int>(strtof(valueBuf, nullptr));
      NRF_LOG_INFO("[AMS] Duration: %d", trackLength);
    }
  } else if (entityId == static_cast<uint8_t>(EntityId::Player)) {
    if (attributeId == static_cast<uint8_t>(PlayerAttributeId::PlaybackInfo)) {
      // Format: "PlaybackState,PlaybackRate,ElapsedTime"
      char* token = valueBuf;
      char* comma = strchr(token, ',');

      int playbackState = 0;
      float rate = 1.0f;
      float elapsed = 0.0f;

      if (comma != nullptr) {
        *comma = '\0';
        playbackState = atoi(token);
        token = comma + 1;
        comma = strchr(token, ',');

        if (comma != nullptr) {
          *comma = '\0';
          rate = strtof(token, nullptr);
          token = comma + 1;
          elapsed = strtof(token, nullptr);
        }
      }

      playing = (playbackState == 1); // PlaybackStatePlaying
      playbackSpeed = rate;
      trackProgress = static_cast<int>(elapsed);
      trackProgressUpdateTime = xTaskGetTickCount();

      NRF_LOG_INFO("[AMS] PlaybackInfo: state=%d rate=%d elapsed=%d", playbackState, static_cast<int>(rate * 100), trackProgress);
    } else if (attributeId == static_cast<uint8_t>(PlayerAttributeId::Volume)) {
      volume = strtof(valueBuf, nullptr);
      NRF_LOG_INFO("[AMS] Volume: %d", static_cast<int>(volume * 100));
    }
  }
  // Queue entity updates (shuffle/repeat) are received but not currently
  // exposed to the Music screen, so we just log them.
}

void AppleMediaServiceClient::sendCommand(uint8_t command) {
  uint16_t connectionHandle = nimble.connHandle();
  if (connectionHandle == 0 || connectionHandle == BLE_HS_CONN_HANDLE_NONE) {
    return;
  }
  if (remoteCommandHandle == 0) {
    return;
  }
  ble_gattc_write_flat(connectionHandle, remoteCommandHandle, &command, 1, nullptr, nullptr);
}

void AppleMediaServiceClient::Reset() {
  amsStartHandle = 0;
  amsEndHandle = 0;
  remoteCommandHandle = 0;
  entityUpdateHandle = 0;
  entityAttributeHandle = 0;
  remoteCommandDescriptorHandle = 0;
  entityUpdateDescriptorHandle = 0;
  isDiscovered = false;
  isRemoteCommandCharDiscovered = false;
  isEntityUpdateCharDiscovered = false;
  isEntityAttributeCharDiscovered = false;
  isRemoteCommandDescriptorFound = false;
  isEntityUpdateDescriptorFound = false;
  subscriptionsDone = false;
  entityUpdatesRegistered = false;

  artistName.clear();
  albumName.clear();
  trackName.clear();
  playing = false;
  playbackSpeed = 1.0f;
  trackProgress = 0;
  trackLength = 0;
  volume = 0.0f;
  trackProgressUpdateTime = 0;
}

std::string AppleMediaServiceClient::getArtist() const {
  return artistName;
}

std::string AppleMediaServiceClient::getTrack() const {
  return trackName;
}

std::string AppleMediaServiceClient::getAlbum() const {
  return albumName;
}

bool AppleMediaServiceClient::isPlaying() const {
  return playing;
}

float AppleMediaServiceClient::getPlaybackSpeed() const {
  return playbackSpeed;
}

int AppleMediaServiceClient::getProgress() const {
  if (playing) {
    return trackProgress +
           static_cast<int>((static_cast<float>(xTaskGetTickCount() - trackProgressUpdateTime) / 1024.0f) * getPlaybackSpeed());
  }
  return trackProgress;
}

int AppleMediaServiceClient::getTrackLength() const {
  return trackLength;
}

bool AppleMediaServiceClient::isActive() const {
  return isDiscovered && subscriptionsDone;
}
