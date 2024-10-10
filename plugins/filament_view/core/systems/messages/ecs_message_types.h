#pragma once
namespace plugin_filament_view {

enum class ECSMessageType {
  DebugLine,

  CollisionRequest,
  CollisionRequestRequestor,
  CollisionRequestType,

  ViewTargetCreateRequest,
  ViewTargetCreateRequestTop,
  ViewTargetCreateRequestLeft,
  ViewTargetCreateRequestWidth,
  ViewTargetCreateRequestHeight,

  SetupMessageChannels,

  ViewTargetStartRenderingLoops,

  SetCameraFromDeserializedLoad
};

}