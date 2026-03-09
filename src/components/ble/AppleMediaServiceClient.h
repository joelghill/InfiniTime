#pragma once

#include <cstdint>
#include <functional>
#include <string>
#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#undef max
#undef min
#include "components/ble/BleClient.h"
#include <FreeRTOS.h>

namespace Pinetime {
  namespace Controllers {
    class NimbleController;

    class AppleMediaServiceClient : public BleClient {
    public:
      explicit AppleMediaServiceClient(NimbleController& nimble);

      bool OnDiscoveryEvent(uint16_t connectionHandle, const ble_gatt_error* error, const ble_gatt_svc* service);
      int OnCharacteristicsDiscoveryEvent(uint16_t connectionHandle, const ble_gatt_error* error, const ble_gatt_chr* characteristic);
      int OnDescriptorDiscoveryEventCallback(uint16_t connectionHandle,
                                             const ble_gatt_error* error,
                                             uint16_t characteristicValueHandle,
                                             const ble_gatt_dsc* descriptor);
      int OnSubscribeCallback(uint16_t connectionHandle, const ble_gatt_error* error, ble_gatt_attr* attribute);
      int OnEntityUpdateWriteCallback(uint16_t connectionHandle, const ble_gatt_error* error, ble_gatt_attr* attribute);
      void OnNotification(ble_gap_event* event);
      void MaybeFinishDiscovery(uint16_t connectionHandle);
      void Reset();
      void Discover(uint16_t connectionHandle, std::function<void(uint16_t)> lambda) override;

      std::string getArtist() const;
      std::string getTrack() const;
      std::string getAlbum() const;
      int getProgress() const;
      int getTrackLength() const;
      float getPlaybackSpeed() const;
      bool isPlaying() const;
      bool isActive() const;

      void sendCommand(uint8_t command);

      static constexpr uint8_t RemoteCommandPlay = 0;
      static constexpr uint8_t RemoteCommandPause = 1;
      static constexpr uint8_t RemoteCommandTogglePlayPause = 2;
      static constexpr uint8_t RemoteCommandNextTrack = 3;
      static constexpr uint8_t RemoteCommandPreviousTrack = 4;
      static constexpr uint8_t RemoteCommandVolumeUp = 5;
      static constexpr uint8_t RemoteCommandVolumeDown = 6;

      // 89D3502B-0F36-433A-8EF4-C502AD55F8DC
      static constexpr ble_uuid128_t amsUuid {
        .u {.type = BLE_UUID_TYPE_128},
        .value = {0xDC, 0xF8, 0x55, 0xAD, 0x02, 0xC5, 0xF4, 0x8E, 0x3A, 0x43, 0x36, 0x0F, 0x2B, 0x50, 0xD3, 0x89}};

    private:
      // 9B3C81D8-57B1-4A8A-B8DF-0E56F7CA51C2
      static constexpr ble_uuid128_t remoteCommandChar {
        .u {.type = BLE_UUID_TYPE_128},
        .value = {0xC2, 0x51, 0xCA, 0xF7, 0x56, 0x0E, 0xDF, 0xB8, 0x8A, 0x4A, 0xB1, 0x57, 0xD8, 0x81, 0x3C, 0x9B}};

      // 2F7CABCE-808D-411F-9A0C-BB92BA96C102
      static constexpr ble_uuid128_t entityUpdateChar {
        .u {.type = BLE_UUID_TYPE_128},
        .value = {0x02, 0xC1, 0x96, 0xBA, 0x92, 0xBB, 0x0C, 0x9A, 0x1F, 0x41, 0x8D, 0x80, 0xCE, 0xAB, 0x7C, 0x2F}};

      // C6B2F38C-23AB-46D8-A6AB-A3A870BBD5D7
      static constexpr ble_uuid128_t entityAttributeChar {
        .u {.type = BLE_UUID_TYPE_128},
        .value = {0xD7, 0xD5, 0xBB, 0x70, 0xA8, 0xA3, 0xAB, 0xA6, 0xD8, 0x46, 0xAB, 0x23, 0x8C, 0xF3, 0xB2, 0xC6}};

      enum class EntityId : uint8_t { Player = 0, Queue = 1, Track = 2 };

      enum class PlayerAttributeId : uint8_t { Name = 0, PlaybackInfo = 1, Volume = 2 };

      enum class QueueAttributeId : uint8_t { Index = 0, Count = 1, ShuffleMode = 2, RepeatMode = 3 };

      enum class TrackAttributeId : uint8_t { Artist = 0, Album = 1, Title = 2, Duration = 3 };

      void RegisterEntityUpdates(uint16_t connectionHandle);
      void ParseEntityUpdate(os_mbuf* om, uint16_t length);

      NimbleController& nimble;

      bool isDiscovered {false};
      bool isRemoteCommandCharDiscovered {false};
      bool isEntityUpdateCharDiscovered {false};
      bool isEntityAttributeCharDiscovered {false};
      bool isRemoteCommandDescriptorFound {false};
      bool isEntityUpdateDescriptorFound {false};
      bool subscriptionsDone {false};
      bool entityUpdatesRegistered {false};

      uint16_t amsStartHandle {0};
      uint16_t amsEndHandle {0};
      uint16_t remoteCommandHandle {0};
      uint16_t entityUpdateHandle {0};
      uint16_t entityAttributeHandle {0};
      uint16_t remoteCommandDescriptorHandle {0};
      uint16_t entityUpdateDescriptorHandle {0};

      std::string artistName;
      std::string albumName;
      std::string trackName;
      bool playing {false};
      float playbackSpeed {1.0f};
      int trackProgress {0};
      int trackLength {0};
      float volume {0.0f};
      TickType_t trackProgressUpdateTime {0};

      std::function<void(uint16_t)> onServiceDiscovered;
    };
  }
}
