/*
 * Copyright 2020-2024 Toyota Connected North America
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "filament_view_plugin.h"

#include <asio/post.hpp>
#include <core/components/derived/camera.h>
#include <core/components/derived/transform.h>
#include <core/scene/serialization/scene_text_deserializer.h>
#include <core/systems/derived/animation_system.h>
#include <core/systems/derived/collision_system.h>
#include <core/systems/derived/debug_lines_system.h>
#include <core/systems/derived/filament_system.h>
#include <core/systems/derived/indirect_light_system.h>
#include <core/systems/derived/light_system.h>
#include <core/systems/derived/model_system.h>
#include <core/systems/derived/shape_system.h>
#include <core/systems/derived/skybox_system.h>
#include <core/systems/derived/transform_system.h>
#include <core/systems/derived/view_target_system.h>
#include <core/systems/ecs.h>
#include <event_sink.h>
#include <event_stream_handler_functions.h>
#include <messages.g.h>
#include <plugins/common/common.h>

class FlutterView;

class Display;

namespace plugin_filament_view {

std::unique_ptr<SceneTextDeserializer> sceneTextDeserializer;
SceneTextDeserializer* postSetupDeserializer = nullptr;
bool m_bHasSetupRegistrar = false;

//////////////////////////////////////////////////////////////////////////////////////////
void RunOnceCheckAndInitializeECSystems() {
  const auto ecs = ECSManager::GetInstance();

  if (ecs->getRunState() != ECSManager::RunState::NotInitialized) {
    return;
  }

  // Get the strand from the ECSManager
  const auto& strand = *ecs->getStrand();

  std::promise<void> initPromise;
  const std::future<void> initFuture = initPromise.get_future();

  // Post the initialization code to the strand
  post(strand, [=, &initPromise]() mutable {
    // Add systems to the ECSManager
    ecs->addSystem(std::make_unique<FilamentSystem>());
    ecs->addSystem(std::make_unique<DebugLinesSystem>());
    ecs->addSystem(std::make_unique<CollisionSystem>());
    ecs->addSystem(std::make_unique<ModelSystem>());
    ecs->addSystem(std::make_unique<MaterialSystem>());
    ecs->addSystem(std::make_unique<ShapeSystem>());
    ecs->addSystem(std::make_unique<IndirectLightSystem>());
    ecs->addSystem(std::make_unique<SkyboxSystem>());
    ecs->addSystem(std::make_unique<LightSystem>());
    ecs->addSystem(std::make_unique<ViewTargetSystem>());
    ecs->addSystem(std::make_unique<AnimationSystem>());
    ecs->addSystem(std::make_unique<TransformSystem>());
    // Internal debate whether we auto subscribe to systems on entity creation
    // or not.

    ecs->initialize();

    initPromise.set_value();
  });

  initFuture.wait();
}

//////////////////////////////////////////////////////////////////////////////////////////
void KickOffRenderingLoops() {
  ECSMessage viewTargetStartRendering;
  viewTargetStartRendering.addData(ECSMessageType::ViewTargetStartRenderingLoops, true);
  ECSManager::GetInstance()->RouteMessage(viewTargetStartRendering);
}

//////////////////////////////////////////////////////////////////////////////////////////
void DeserializeDataAndSetupMessageChannels(
  flutter::PluginRegistrar* registrar,
  const std::vector<uint8_t>& params
) {
  const auto ecs = ECSManager::GetInstance();

  // Get the strand from the ECSManager
  const auto& strand = *ecs->getStrand();

  std::promise<void> initPromise;
  const std::future<void> initFuture = initPromise.get_future();

  // Safeguarded to only be called once no matter how many times this method is
  // called.
  if (postSetupDeserializer == nullptr) {
    post(strand, [=, &initPromise]() mutable {
      spdlog::trace("running SceneTextDeserializer");
      sceneTextDeserializer = std::make_unique<SceneTextDeserializer>(params);
      postSetupDeserializer = sceneTextDeserializer.get();

      // making sure this is only called once!
      postSetupDeserializer->RunPostSetupLoad();

      initPromise.set_value();
    });

    initFuture.wait();
  }

  spdlog::trace("getting systems");
  const auto animationSystem = ecs->getSystem<AnimationSystem>(__FUNCTION__);

  const auto viewTargetSystem = ecs->getSystem<ViewTargetSystem>(__FUNCTION__);

  const auto collisionSystem = ecs->getSystem<CollisionSystem>(__FUNCTION__);

  collisionSystem->setupMessageChannels(registrar, "plugin.filament_view.collision_info");
  viewTargetSystem->setupMessageChannels(registrar, "plugin.filament_view.frame_view");
  animationSystem->setupMessageChannels(registrar, "plugin.filament_view.animation_info");
}

//////////////////////////////////////////////////////////////////////////////////////////
void FilamentViewPlugin::RegisterWithRegistrar(
  flutter::PluginRegistrar* registrar,
  int32_t id,
  std::string viewType,
  int32_t direction,
  double top,
  double left,
  double width,
  double height,
  const std::vector<uint8_t>& params,
  const std::string& assetDirectory,
  FlutterDesktopEngineRef engine,
  PlatformViewAddListener addListener,
  PlatformViewRemoveListener removeListener,
  void* platform_view_context
) {
  pthread_setname_np(pthread_self(), "HomeScreenFilamentViewPlugin");

  const auto ecs = ECSManager::GetInstance();
  ecs->setConfigValue(kAssetPath, assetDirectory);

  /*bool bDebugAttached = false;
  int i = 0;
  while (!bDebugAttached) {
    int breakhere = 0;
    if (i++ == 10000000000) {
      bDebugAttached = true;
    }
  }*/

  // Safeguarded inside
  RunOnceCheckAndInitializeECSystems();

  // Every time this method is called, we should create a new view target
  ECSMessage viewTargetCreationRequest;
  viewTargetCreationRequest.addData(ECSMessageType::ViewTargetCreateRequest, engine);
  viewTargetCreationRequest.addData(
    ECSMessageType::ViewTargetCreateRequestTop, static_cast<int>(top)
  );
  viewTargetCreationRequest.addData(
    ECSMessageType::ViewTargetCreateRequestLeft, static_cast<int>(left)
  );
  viewTargetCreationRequest.addData(
    ECSMessageType::ViewTargetCreateRequestWidth, static_cast<uint32_t>(width)
  );
  viewTargetCreationRequest.addData(
    ECSMessageType::ViewTargetCreateRequestHeight, static_cast<uint32_t>(height)
  );

  ecs->RouteMessage(viewTargetCreationRequest);

  // Safeguarded to only be called once internal
  DeserializeDataAndSetupMessageChannels(registrar, params);

  if (!m_bHasSetupRegistrar) {
    m_bHasSetupRegistrar = true;

    //
    auto plugin = std::make_unique<FilamentViewPlugin>(
      id, std::move(viewType), direction, top, left, width, height, params, assetDirectory,
      addListener, removeListener, platform_view_context
    );

    // Set up message channels and APIs
    SetUp(registrar->messenger(), plugin.get());

    registrar->AddPlugin(std::move(plugin));

    setupMessageChannels(registrar);
  }

  // Ok to be called infinite times.
  KickOffRenderingLoops();

  spdlog::debug("Initialization completed");
}

//////////////////////////////////////////////////////////////////////////////////////////
FilamentViewPlugin::FilamentViewPlugin(
  const int32_t id,
  std::string viewType,
  const int32_t direction,
  const double top,
  const double left,
  const double width,
  const double height,
  const std::vector<uint8_t>& /*params*/,
  const std::string& /*assetDirectory*/,
  const PlatformViewAddListener addListener,
  const PlatformViewRemoveListener removeListener,
  void* platform_view_context
)
  : PlatformView(id, std::move(viewType), direction, top, left, width, height),
    id_(id),
    platformViewsContext_(platform_view_context),
    removeListener_(removeListener) {
  SPDLOG_TRACE("++FilamentViewPlugin::FilamentViewPlugin");

  addListener(platformViewsContext_, id, &platform_view_listener_, this);

  SPDLOG_TRACE("--FilamentViewPlugin::FilamentViewPlugin");
}

//////////////////////////////////////////////////////////////////////////////////////////
FilamentViewPlugin::~FilamentViewPlugin() {
  removeListener_(platformViewsContext_, id_);

  ECSManager::GetInstance()->destroy();
  // wait for thread to stop running. (Should be relatively quick)
  while (ECSManager::GetInstance()->bIsCompletedStopping() == false) {
  }
}

//////////////////////////////////////////////////////////////////////////////////////////
std::unique_ptr<flutter::EventSink<>> eventSink_;
void FilamentViewPlugin::setupMessageChannels(flutter::PluginRegistrar* registrar) {
  // Setup MethodChannel for readiness check
  const std::string readinessMethodChannel = "plugin.filament_view.readiness_checker";

  const auto methodChannel = std::make_unique<flutter::MethodChannel<>>(
    registrar->messenger(), readinessMethodChannel, &flutter::StandardMethodCodec::GetInstance()
  );

  methodChannel->SetMethodCallHandler(
    [&](const flutter::MethodCall<>& call, const std::unique_ptr<flutter::MethodResult<>>& result) {
      if (call.method_name() == "isReady") {
        // Check readiness and respond
        bool isReady = true;  // Replace with your actual readiness check
        result->Success(flutter::EncodableValue(isReady));
      } else {
        result->NotImplemented();
      }
    }
  );

  // Setup EventChannel for readiness events
  const std::string readinessEventChannel = "plugin.filament_view.readiness";

  const auto eventChannel = std::make_unique<flutter::EventChannel<>>(
    registrar->messenger(), readinessEventChannel, &flutter::StandardMethodCodec::GetInstance()
  );

  eventChannel->SetStreamHandler(std::make_unique<flutter::StreamHandlerFunctions<>>(
    [&](
      const flutter::EncodableValue* /* arguments */, std::unique_ptr<flutter::EventSink<>>&& events
    ) -> std::unique_ptr<flutter::StreamHandlerError<>> {
      eventSink_ = std::move(events);
      sendReadyEvent();  // Proactively send "ready" event
      return nullptr;
    },
    [&](const flutter::EncodableValue* /* arguments */)
      -> std::unique_ptr<flutter::StreamHandlerError<>> {
      eventSink_ = nullptr;
      return nullptr;
    }
  ));
}

//////////////////////////////////////////////////////////////////////////////////////////
void FilamentViewPlugin::sendReadyEvent() {
  if (eventSink_) {
    eventSink_->Success(flutter::EncodableValue("ready"));
  }
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::ChangeMaterialParameter(
  const flutter::EncodableMap& params,
  const int64_t guid
) {
  ECSMessage materialData;
  materialData.addData(ECSMessageType::ChangeMaterialParameter, params);
  materialData.addData(ECSMessageType::EntityToTarget, guid);
  ECSManager::GetInstance()->RouteMessage(materialData);
  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::ChangeMaterialDefinition(
  const flutter::EncodableMap& params,
  const int64_t guid
) {
  ECSMessage materialData;
  materialData.addData(ECSMessageType::ChangeMaterialDefinition, params);
  materialData.addData(ECSMessageType::EntityToTarget, guid);
  ECSManager::GetInstance()->RouteMessage(materialData);
  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::ToggleShapesInScene(const bool value) {
  ECSMessage toggleMessage;
  toggleMessage.addData(ECSMessageType::ToggleShapesInScene, value);
  ECSManager::GetInstance()->RouteMessage(toggleMessage);
  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::ToggleDebugCollidableViewsInScene(const bool value
) {
  ECSMessage toggleMessage;
  toggleMessage.addData(ECSMessageType::ToggleDebugCollidableViewsInScene, value);
  ECSManager::GetInstance()->RouteMessage(toggleMessage);
  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::ChangeViewQualitySettings() {
  static int qualitySettingsVal = 0;
  ECSMessage qualitySettings;
  qualitySettings.addData(ECSMessageType::ChangeViewQualitySettings, qualitySettingsVal);
  ECSManager::GetInstance()->RouteMessage(qualitySettings);

  qualitySettingsVal++;
  if (qualitySettingsVal > ViewTarget::ePredefinedQualitySettings::Ultra) {
    qualitySettingsVal = 0;
  }
  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::SetFogOptions(const bool enabled) {
  ECSMessage fogData;
  fogData.addData(ECSMessageType::SetFogOptions, enabled);
  ECSManager::GetInstance()->RouteMessage(fogData);

  return std::nullopt;
}

/*
 *  Camera
 */
std::optional<FlutterError> FilamentViewPlugin::SetCameraOrbit(
  int64_t id,
  int64_t origin_entity_id,
  const std::vector<double>& orbit_rotation
) {
  spdlog::trace("SetCameraOrbit");

  const auto ecs = ECSManager::GetInstance();
  // Get camera component
  const auto camera = ecs->getComponent<Camera>(id);

  const auto oldTarget = camera->orbitOriginEntity;
  camera->orbitOriginEntity = origin_entity_id;
  camera->orbitRotation = filament::math::quatf(
    orbit_rotation[3], orbit_rotation[0], orbit_rotation[1], orbit_rotation[2]
  );

  if (oldTarget != origin_entity_id) {
    spdlog::debug("Camera target set to entity: {}", origin_entity_id);
  }

  return std::nullopt;
}

std::optional<FlutterError> FilamentViewPlugin::SetCameraTarget(
  int64_t id,
  int64_t target_entity_id,
  const std::vector<double>* target_position
) {
  spdlog::trace("SetCameraTarget");

  const auto ecs = ECSManager::GetInstance();
  // Get camera component
  const auto camera = ecs->getComponent<Camera>(id);

  camera->enableTarget = !!target_position && target_entity_id != kNullGuid;
  camera->targetEntity = target_entity_id;
  if (target_position) {
    camera->targetPoint = filament::math::float3(
      (*target_position)[0], (*target_position)[1], (*target_position)[2]
    );
  }

  spdlog::debug("Camera target set to entity: {}", target_entity_id);
  return std::nullopt;
}

std::optional<FlutterError> FilamentViewPlugin::SetActiveCamera(
  const int64_t* view_id,
  int64_t camera_id
) {
  spdlog::trace("SetActiveCamera");

  size_t viewIndex = view_id != nullptr ? static_cast<size_t>(*view_id) : 0;
  EntityGUID cameraId = camera_id;

  const auto viewSystem = ECSManager::GetInstance()->getSystem<ViewTargetSystem>(__FUNCTION__);
  viewSystem->setViewCamera(viewIndex, cameraId);

  spdlog::debug("Camera {} set as active for view {}", cameraId, viewIndex);
  return std::nullopt;
}

std::optional<FlutterError> FilamentViewPlugin::SetCameraDolly(
  int64_t id,
  const std::vector<double>& dolly_offset
) {
  spdlog::trace("SetCameraDolly");

  const auto ecs = ECSManager::GetInstance();
  // Get camera component
  const auto camera = ecs->getComponent<Camera>(id);

  camera->dollyOffset = filament::math::float3(dolly_offset[0], dolly_offset[1], dolly_offset[2]);

  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::ChangeLightTransformByGUID(
  const int64_t guid,
  double posx,
  double posy,
  double posz,
  double dirx,
  double diry,
  double dirz
) {
  filament::math::float3 position(
    static_cast<float>(posx), static_cast<float>(posy), static_cast<float>(posz)
  );
  filament::math::float3 direction(
    static_cast<float>(dirx), static_cast<float>(diry), static_cast<float>(dirz)
  );

  ECSMessage lightData;
  lightData.addData(ECSMessageType::ChangeSceneLightTransform, guid);
  lightData.addData(ECSMessageType::Position, position);
  lightData.addData(ECSMessageType::Direction, direction);
  ECSManager::GetInstance()->RouteMessage(lightData);

  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::ChangeLightColorByGUID(
  const int64_t guid,
  const std::string& color,
  const int64_t intensity
) {
  ECSMessage lightData;
  lightData.addData(ECSMessageType::ChangeSceneLightProperties, guid);
  lightData.addData(ECSMessageType::ChangeSceneLightPropertiesColorValue, color);
  lightData.addData(
    ECSMessageType::ChangeSceneLightPropertiesIntensity, static_cast<float>(intensity)
  );
  ECSManager::GetInstance()->RouteMessage(lightData);

  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::EnqueueAnimation(
  const int64_t guid,
  const int64_t animation_index
) {
  ECSMessage enqueueMessage;
  enqueueMessage.addData(ECSMessageType::AnimationEnqueue, static_cast<int32_t>(animation_index));
  enqueueMessage.addData(ECSMessageType::EntityToTarget, guid);
  ECSManager::GetInstance()->RouteMessage(enqueueMessage);

  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::ClearAnimationQueue(const int64_t guid) {
  ECSMessage clearQueueMessage;
  clearQueueMessage.addData(ECSMessageType::AnimationClearQueue, guid);
  clearQueueMessage.addData(ECSMessageType::EntityToTarget, guid);
  ECSManager::GetInstance()->RouteMessage(clearQueueMessage);

  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::PlayAnimation(
  const int64_t guid,
  const int64_t animation_index
) {
  ECSMessage playMessage;
  playMessage.addData(ECSMessageType::AnimationPlay, static_cast<int32_t>(animation_index));
  playMessage.addData(ECSMessageType::EntityToTarget, guid);
  ECSManager::GetInstance()->RouteMessage(playMessage);

  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::ChangeAnimationSpeed(
  const int64_t guid,
  const double speed
) {
  ECSMessage changeSpeedMessage;
  changeSpeedMessage.addData(ECSMessageType::AnimationChangeSpeed, static_cast<float>(speed));
  changeSpeedMessage.addData(ECSMessageType::EntityToTarget, guid);
  ECSManager::GetInstance()->RouteMessage(changeSpeedMessage);

  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::PauseAnimation(const int64_t guid) {
  ECSMessage pauseMessage;
  pauseMessage.addData(ECSMessageType::AnimationPause, guid);
  pauseMessage.addData(ECSMessageType::EntityToTarget, guid);
  ECSManager::GetInstance()->RouteMessage(pauseMessage);

  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::ResumeAnimation(const int64_t guid) {
  ECSMessage resumeMessage;
  resumeMessage.addData(ECSMessageType::AnimationResume, guid);
  resumeMessage.addData(ECSMessageType::EntityToTarget, guid);
  ECSManager::GetInstance()->RouteMessage(resumeMessage);

  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::SetAnimationLooping(
  const int64_t guid,
  const bool looping
) {
  ECSMessage setLoopingMessage;
  setLoopingMessage.addData(ECSMessageType::AnimationSetLooping, looping);
  setLoopingMessage.addData(ECSMessageType::EntityToTarget, guid);
  ECSManager::GetInstance()->RouteMessage(setLoopingMessage);

  return std::nullopt;
}

std::optional<FlutterError> FilamentViewPlugin::RaycastFromTap(double x, double y) {
  asio::post(*ECSManager::GetInstance()->getStrand(), [&, x, y] {
    const auto ecs = ECSManager::GetInstance();
    const auto viewTargetSystem = ecs->getSystem<ViewTargetSystem>(
      "FilamentViewPlugin::RaycastFromTap"
    );
    viewTargetSystem->getMainViewTarget()->onTouch(x, y);
  });

  return std::nullopt;
}

std::optional<FlutterError> FilamentViewPlugin::RequestCollisionCheckFromRay(
  const std::string& query_id,
  double origin_x,
  double origin_y,
  double origin_z,
  double direction_x,
  double direction_y,
  double direction_z,
  double length
) {
  filament::math::float3 origin(
    static_cast<float>(origin_x), static_cast<float>(origin_y), static_cast<float>(origin_z)
  );
  filament::math::float3 direction(
    static_cast<float>(direction_x), static_cast<float>(direction_y),
    static_cast<float>(direction_z)
  );

  Ray rayInfo(origin, direction, static_cast<float>(length));

  // Debug line message
  ECSMessage rayInformation;
  rayInformation.addData(ECSMessageType::DebugLine, rayInfo);
  ECSManager::GetInstance()->RouteMessage(rayInformation);

  // Collision request message
  ECSMessage collisionRequest;
  collisionRequest.addData(ECSMessageType::CollisionRequest, rayInfo);
  collisionRequest.addData(ECSMessageType::CollisionRequestRequestor, query_id);
  collisionRequest.addData(ECSMessageType::CollisionRequestType, eFromNonNative);
  ECSManager::GetInstance()->RouteMessage(collisionRequest);

  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::SetEntityTransformScale(
  const int64_t guid,
  const std::vector<double>& scl
) {
  ECSManager::GetInstance()->getComponent<Transform>(guid)->setScale({scl[0], scl[1], scl[2]});

  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::SetEntityTransformPosition(
  const int64_t guid,
  const std::vector<double>& pos
) {
  ECSManager::GetInstance()->getComponent<Transform>(guid)->setPosition({pos[0], pos[1], pos[2]});

  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::SetEntityTransformRotation(
  const int64_t guid,
  const std::vector<double>& rot
) {
  ECSManager::GetInstance()->getComponent<Transform>(guid)->setRotation(
    /// NOTE: Filament quat constructor takes WXYZ!!! It's still stored in memory as XYZW.
    /// TODO: when Float32List is supported in pigeon, just cast the array to quatf
    {rot[3], rot[0], rot[1], rot[2]}
  );

  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::TurnOffVisualForEntity(const int64_t guid) {
  ECSMessage changeRequest;
  changeRequest.addData(ECSMessageType::ToggleVisualForEntity, guid);
  changeRequest.addData(ECSMessageType::BoolValue, false);
  ECSManager::GetInstance()->RouteMessage(changeRequest);

  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::TurnOnVisualForEntity(const int64_t guid) {
  ECSMessage changeRequest;
  changeRequest.addData(ECSMessageType::ToggleVisualForEntity, guid);
  changeRequest.addData(ECSMessageType::BoolValue, true);
  ECSManager::GetInstance()->RouteMessage(changeRequest);

  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::TurnOffCollisionChecksForEntity(const int64_t guid
) {
  ECSMessage changeRequest;
  changeRequest.addData(ECSMessageType::ToggleCollisionForEntity, guid);
  changeRequest.addData(ECSMessageType::BoolValue, false);
  ECSManager::GetInstance()->RouteMessage(changeRequest);

  return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////////////////////
std::optional<FlutterError> FilamentViewPlugin::TurnOnCollisionChecksForEntity(const int64_t guid) {
  ECSMessage changeRequest;
  changeRequest.addData(ECSMessageType::ToggleCollisionForEntity, guid);
  changeRequest.addData(ECSMessageType::BoolValue, true);
  ECSManager::GetInstance()->RouteMessage(changeRequest);

  return std::nullopt;
}

// TODO this function will need to change to say 'which' view is being changed.
void FilamentViewPlugin::on_resize(const double width, const double height, void* /*data*/) {
  if (width <= 0 || height <= 0) {
    return;
  }

  ECSMessage resizeMessage;
  resizeMessage.addData(ECSMessageType::ResizeWindow, static_cast<size_t>(0));
  resizeMessage.addData(ECSMessageType::ResizeWindowWidth, width);
  resizeMessage.addData(ECSMessageType::ResizeWindowHeight, height);
  ECSManager::GetInstance()->RouteMessage(resizeMessage);
}

void FilamentViewPlugin::on_set_direction(const int32_t direction, void* data) {
  const auto plugin = static_cast<FilamentViewPlugin*>(data);
  if (plugin) {
    plugin->direction_ = direction;
  }
  SPDLOG_TRACE("SetDirection: {}", plugin->direction_);
}

// TODO this function will need to change to say 'which' view is being changed.
void FilamentViewPlugin::on_set_offset(const double left, const double top, void* /*data*/) {
  ECSMessage moveMessage;
  moveMessage.addData(ECSMessageType::MoveWindow, static_cast<size_t>(0));
  moveMessage.addData(ECSMessageType::MoveWindowLeft, left);
  moveMessage.addData(ECSMessageType::MoveWindowTop, top);
  ECSManager::GetInstance()->RouteMessage(moveMessage);
}

void FilamentViewPlugin::on_touch(
  const int32_t /*action*/,
  const int32_t /*point_count*/,
  const size_t /*point_data_size*/,
  const double* /*point_data*/,
  void* /*data*/
) {
  // NOTE: unused! all touch events are handled by Flutter and passed to handlers as necessary
}

void FilamentViewPlugin::on_dispose(bool /* hybrid */, void* /* data */) {
  spdlog::warn("[FilamentViewPlugin] on_dispose not currently implemented");
}

const platform_view_listener FilamentViewPlugin::platform_view_listener_ = {
  .resize = on_resize,
  .set_direction = on_set_direction,
  .set_offset = on_set_offset,
  .on_touch = on_touch,
  .dispose = on_dispose,
  .accept_gesture = nullptr,
  .reject_gesture = nullptr,
};

}  // namespace plugin_filament_view
