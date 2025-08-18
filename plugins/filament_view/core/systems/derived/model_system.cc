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
#include "model_system.h"
#include "collision_system.h"

#include <algorithm>  // for max
#include <asio/post.hpp>
#include <cassert>
#include <core/components/derived/collider.h>
#include <core/entity/base/entityobject.h>
#include <core/include/file_utils.h>
#include <core/systems/derived/transform_system.h>
#include <core/systems/ecs.h>
#include <curl_client/curl_client.h>
#include <filament/Scene.h>
#include <filament/gltfio/ResourceLoader.h>
#include <filament/gltfio/TextureProvider.h>
#include <filament/gltfio/materials/uberarchive.h>
#include <filament/utils/Slice.h>

// rapidjson
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/rapidjson.h>
#include <rapidjson/stringbuffer.h>

#include "animation_system.h"

namespace plugin_filament_view {

using filament::gltfio::AssetConfiguration;
using filament::gltfio::AssetLoader;
using filament::gltfio::ResourceConfiguration;
using filament::gltfio::ResourceLoader;

////////////////////////////////////////////////////////////////////////////////////
void ModelSystem::destroyAllAssetsOnModels() {
  for (const auto& [fst, snd] : _models) {
    destroyAsset(snd->getAsset());  // NOLINT
  }
  _models.clear();
}

////////////////////////////////////////////////////////////////////////////////////
void ModelSystem::destroyAsset(const filament::gltfio::FilamentAsset* asset) const {
  if (!asset) {
    return;
  }

  _filament->getFilamentScene()->removeEntities(asset->getEntities(), asset->getEntityCount());
  assetLoader_->destroyAsset(asset);
}

void ModelSystem::createModelInstance(Model* model) {
  assertInitialized();
  debug_assert(assetLoader_ != nullptr, "ModelSystem::createModelInstance - assetLoader_ is null");

  const auto assetPath = model->getAssetPath();
  spdlog::trace("\nModelSystem::createModelInstance: {}", assetPath);
  spdlog::trace("instance mode: {}", modelInstancingModeToString(model->getInstancingMode()));

  // NOTE: if you're creating a lot of instances, this is better to use at the
  // start FilamentAsset* createInstancedAsset(const uint8_t* bytes, uint32_t
  // numBytes, FilamentInstance **instances, size_t numInstances)
  /// NOTE: is it tho?
  filament::gltfio::FilamentAsset* asset = nullptr;
  filament::gltfio::FilamentInstance* assetInstance = nullptr;

  // check if instanceable (primary) is loaded
  const auto instancedModelData = _assets[assetPath];
  if (instancedModelData.state != AssetLoadingState::unset) {
    spdlog::trace("Primary confirmed loaded");
    asset = instancedModelData.asset;
    runtime_assert(asset != nullptr, "ModelSystem::createModelInstance - asset CANNOT be null");

    // if the asset is not instanceable, it will return nullptr
    assetInstance = assetLoader_->createInstance(asset);
    spdlog::trace("Asset instance created!");
    // if not nullptr, it means it's a valid secondary instance
  } else {
    throw std::runtime_error(
      fmt::format("ModelSystem::createModelInstance - asset {} not loaded", assetPath)
    );
  }

  // instance / secondary object.
  // asset loaded,
  spdlog::trace("HAS INSTANCE");
  model->setAssetInstance(assetInstance);

  // by this point we should either have a subinstance, or THE primary instance
  runtime_assert(assetInstance != nullptr, "This should NEVER be null!");
}

void ModelSystem::addModelToScene(EntityGUID modelGuid) {
  assertInitialized();

  // Get model
  std::shared_ptr<Model> model = _models[modelGuid];
  runtime_assert(
    model != nullptr,
    fmt::format("[{}] Can't add model({}) to scene, model is null", __FUNCTION__, modelGuid).c_str()
  );

  // Expects the model to be already loaded
  runtime_assert(
    _assets[model->getAssetPath()].state == AssetLoadingState::loaded,
    fmt::format(
      "[{}] Can't add model({}) to scene, asset not loaded (asset "
      "state: {})",
      __FUNCTION__, modelGuid, modelInstancingModeToString(model->getInstancingMode())
    )
      .c_str()
  );

  const bool isInScene = model->isInScene();
  const ModelInstancingMode instancingMode = model->getInstancingMode();

  if (isInScene) {
    spdlog::warn(
      "[{}] model '{}'({}) is already in scene (asset {}), skipping add", __FUNCTION__,  //
      model->name, modelGuid, model->assetPath_
    );
    return;
  }

  FilamentEntity* modelEntities = nullptr;
  size_t modelEntityCount = 0;

  // Get the asset instance
  const auto asset = model->getAsset();
  const auto assetInstance = model->getAssetInstance();

  if (instancingMode == ModelInstancingMode::secondary) {
    // If it's a secondary instance, we need to get the entities from the asset
    // instance
    runtime_assert(
      assetInstance != nullptr, "ModelSystem::addModelToScene: model asset instance cannot be null"
    );
    // runtime_assert(
    //   asset != nullptr,
    //   "ModelSystem::addModelToScene: model asset cannot be null"
    // );

    modelEntities = const_cast<FilamentEntity*>(assetInstance->getEntities());
    modelEntityCount = assetInstance->getEntityCount();
  } else {
    /// If it's non-instanced, we need to get the entities from the asset`
    // runtime_assert(
    //   asset != nullptr,
    //   "ModelSystem::addModelToScene: model asset cannot be null"
    // );

    if (asset == nullptr) {
      spdlog::warn(
        "[{}] model({}) asset({}) is null, deferring load till later (are we "
        "tho?)",
        __FUNCTION__, modelGuid, model->getAssetPath()
      );
      return;
    }

    modelEntities = const_cast<FilamentEntity*>(asset->getRenderableEntities());
    modelEntityCount = asset->getRenderableEntityCount();
  }

  /*
   *  Renderable setup
   */
  spdlog::trace("  Setting up renderables...");

  const utils::Slice renderables{modelEntities, modelEntityCount};

  if (instancingMode == ModelInstancingMode::primary) {
    spdlog::trace("  Model({}) is primary, not adding to scene", modelGuid);
    return;
  }

  // Add to ECS
  spdlog::trace("  Adding model({}) to ECS", __FUNCTION__, modelGuid);
  ecs->addEntity(model);

  FilamentEntity instanceEntity = assetInstance->getRoot();
  model->_fEntity = instanceEntity;
  spdlog::trace("  Adding model[{}]->({}) to Filament scene", instanceEntity.getId(), modelGuid);
  _filament->getFilamentScene()->addEntity(instanceEntity);
  model->_childrenEntities[instanceEntity] = modelGuid;

  for (const auto entity : renderables) {
    // const auto entity = modelEntities[i];
    _filament->getFilamentScene()->addEntity(entity);

    setupRenderable(
      entity, model.get(),
      const_cast<filament::gltfio::FilamentAsset*>(model->getAssetInstance()->getAsset())
    );
  }

  // Set up transform parenting (needs to be done after renderable setup)
  spdlog::trace("  Setting up transform parenting for model({})", modelGuid);
  for (auto& [childEntity, childGuid] : model->_childrenEntities) {
    spdlog::trace(
      "  child[{}]->({}) {}", childEntity.getId(), childGuid,
      childGuid == modelGuid ? "(is model!)" : ""
    );

    // Skip the model itself
    if (childGuid == kNullGuid || childGuid == modelGuid) continue;

    const auto childTransform = ecs->getComponent<Transform>(childGuid);
    const FilamentTransformInstance childInstance = _tm->getInstance(childEntity);
    const FilamentEntity parentEntity = _tm->getParent(childInstance);
    const EntityGUID parentGuid = model->_childrenEntities[parentEntity];

    spdlog::trace("    has parent[{}]->({})", parentEntity.getId(), parentGuid);

    if (parentGuid != kNullGuid)  // safeguard, shouldn't be necessary
      childTransform->setParent(parentGuid);
  }

  // Set up transform
  auto transform = model->getComponent<Transform>();
  transform->_fInstance = _tm->getInstance(instanceEntity);
  transform->setDirty(true);
  /// NOTE: why is this needed? if this is not called the collider doesn't work,
  //        even though it's visible
  ecs->getSystem<TransformSystem>("ModelSystem::addModelToScene")
    ->applyTransform(model->getGuid(), true);

  // Set up renderable
  auto renderable = model->getComponent<CommonRenderable>();
  renderable->_fInstance = _rcm->getInstance(instanceEntity);

  // Set up collider
  // NOTE: no need - CollisionSystem sets up colliders asynchronously on
  // update

  // Set up animator
  filament::gltfio::Animator* animatorInstance = nullptr;
  if (assetInstance != nullptr) {
    animatorInstance = assetInstance->getAnimator();
  } else if (asset != nullptr) {
    animatorInstance = asset->getInstance()->getAnimator();
  }
  if (animatorInstance != nullptr && model->hasComponent<Animation>()) {
    const auto animator = model->getComponent<Animation>();
    animator->setAnimator(*animatorInstance);

    // Great if you need help with your animation information!
    // animationPtr->debugPrint("From ModelSystem::addModelToScene\t");
  } else if (animatorInstance != nullptr && animatorInstance->getAnimationCount() > 0) {
    SPDLOG_DEBUG(
      "For asset - {} you have a valid set of animations [{}] you can play "
      "on this, but you didn't load an animation component, load one if you "
      "want that "
      "functionality",
      model->getAssetPath(), animatorInstance->getAnimationCount()
    );
  }

  model->m_isInScene = true;
}

void ModelSystem::setupRenderable(
  const FilamentEntity fEntity,
  Model* model,
  filament::gltfio::FilamentAsset* asset
) {
  const char* name = asset->getName(fEntity);
  if (name == nullptr) {
    name = "(null)";
  }

  // Create a RenderableEntityObject child
  const auto child = std::make_shared<RenderableEntityObject>();
  child->_fEntity = fEntity;
  child->name = asset->getName(fEntity);
  spdlog::trace(
    "  Creating child entity '{}'({})->[{}] of '{}'({})", child->name, child->getGuid(),
    fEntity.getId(), model->name, model->getGuid()
  );
  model->_childrenEntities[fEntity] = child->getGuid();

  /*
   * Transform
   */
  /// NOTE: we set up transform first, even if it might not have a renderable
  ///       because it's still valid for parenting reasons.
  const auto ti = _tm->getInstance(fEntity);
  if (!ti.isValid()) {
    spdlog::trace(
      "[{}] Skipping fentity {} of model({}), has no transform", __FUNCTION__, fEntity.getId(),
      model->getGuid()
    );
    return;
  }

  // Set up Transform component
  auto transform = Transform();
  transform._fInstance = ti;
  transform.setTransform(_tm->getTransform(ti));
  auto parentEntity = _tm->getParent(ti);

  spdlog::trace("  Parent entity: [{}]", parentEntity.getId());
#if SPDLOG_LEVEL == trace
// transform.debugPrint("  ");
#endif

  child->addComponent(transform);

  const auto ri = _rcm->getInstance(fEntity);
  if (!ri.isValid()) {
    spdlog::trace(
      "[{}] Skipping fentity {} of model({}), has no renderable", __FUNCTION__, fEntity.getId(),
      model->getGuid()
    );

    ecs->addEntity(child);
    return;
  }

  const auto commonRenderable = model->getCommonRenderable();
  _rcm->setCastShadows(ri, commonRenderable->IsCastShadowsEnabled());
  _rcm->setReceiveShadows(ri, commonRenderable->IsReceiveShadowsEnabled());
  _rcm->setScreenSpaceContactShadows(ri, false);

  // Set up Renderable component
  auto renderable = CommonRenderable();
  renderable._fInstance = ri;
  child->addComponent(renderable);

  // (optional) Set up Collider component
  // Get extras (aka "userData", aka Blender's "Custom Properties"), string
  // containing JSON If extras present and have "fs_touchEvent" property, parse
  // and setup a Collider component
  if (const char* extras = asset->getExtras(fEntity); !!extras) try {
      // Parse using RapidJSON
      spdlog::debug("  Has extras! Parsing '{}'", extras);
      rapidjson::Document doc;
      doc.Parse(extras);
      if (doc.HasParseError()) {
        spdlog::error(rapidjson::GetParseError_En(doc.GetParseError()));
        throw std::runtime_error("[ModelSystem::setupCollidableChild] Failed to parse extras JSON");
      }

      // Get the touchEvent property
      constexpr char kTouchEventProp[] = "fs_touchEvent";
      if (doc.HasMember(kTouchEventProp)) {
        const auto& touchEvent = doc[kTouchEventProp];
        // type: string (event name)
        const auto& eventName = touchEvent.GetString();
        spdlog::trace("  Has '{}'! Value: {}", kTouchEventProp, eventName);

        // check if is a perfect cube (with epsilon = 0.01)
        // constexpr float epsilon = 0.01f;
        // const AABB aabb = renderable.getAABB();
        // const float sizeX = aabb.halfExtent.x * 2;
        // const float sizeY = aabb.halfExtent.y * 2;
        // const float sizeZ = aabb.halfExtent.z * 2;
        // const bool isCube = std::abs(sizeX - sizeY) < epsilon &&
        //                     std::abs(sizeY - sizeZ) < epsilon;
        // spdlog::trace("isCube: {}", isCube);

        // attach collider
        auto collider = Collider();
        collider.setIsStatic(false);
        collider.eventName = eventName;
        // collider.aabb = aabb;
        // NOTE: extents automatically set from AABB by CollisionSystem
        // collider.setShapeType(isCube ? ShapeType::Cube :
        //                                   ShapeType::Sphere);
        child->addComponent(collider);

        spdlog::trace("  Model child collider setup complete");
      }

    } catch (const std::exception& e) {
      spdlog::error(
        "[{}] Failed to setup collider child({}) with JSON: {}\nReason: {}", __FUNCTION__,
        fEntity.getId(), extras, e.what()
      );
    }

  ecs->addEntity(child);
}

////////////////////////////////////////////////////////////////////////////////////
void ModelSystem::updateAsyncAssetLoading() {
  // spdlog::debug(
  //   "[{}] Updating async asset loading",
  //   __FUNCTION__
  // );

  // This does not specify per resource, but a global, best we can do with this
  // information is if we're done loading <everything> that was marked as async
  // load, then load that physics data onto a collider if required. This gives
  // us visuals without colliders in a scene with <tons> of objects; but would
  // eventually settle
  resourceLoader_->asyncUpdateLoad();
  const float percentComplete = resourceLoader_->asyncGetLoadProgress();
  if (percentComplete != 1.0f) {
    spdlog::trace("[{}] Model async loading progress: {}%", __FUNCTION__, percentComplete * 100.0f);
    return;
  } else {
    // spdlog::info(
    //   "[{}] Async model loading complete",
    //   __FUNCTION__
    // );
  }

  // for each AssetDescriptor...
  for (auto& [assetPath, assetData] : _assets) {
    // if the asset is loaded, mark it as loaded
    if (assetData.state == AssetLoadingState::loading) {
      assetData.state = AssetLoadingState::loaded;
    }

    if (assetData.state == AssetLoadingState::loaded) {
      // if the asset is loaded, load the models
      for (auto& modelGuid : assetData.loadingInstances) {
        // get the model
        std::shared_ptr<Model> model = _models[modelGuid];
        if (model == nullptr) {
          spdlog::error("[{}] Model {} not found", __FUNCTION__, modelGuid);
          continue;
        }

        if (model->isInScene()) {
          spdlog::warn("Model {} is already in scene, skipping load", model->name);
          continue;
        }

        // Add model to scene
        spdlog::debug("Loaded, adding model to scene: '{}'({})", model->getAssetPath(), modelGuid);

        switch (model->getInstancingMode()) {
          case ModelInstancingMode::primary:
            spdlog::trace("Model is primary, updating transform but not adding to scene");
            break;
          case ModelInstancingMode::secondary:
            // load the model as an instance
            spdlog::trace("Loading model as instance: {}", model->getAssetPath());
            createModelInstance(model.get());
            spdlog::trace("Model instanced, adding to scene...");
            addModelToScene(modelGuid);
            spdlog::trace("Model added to scene! Yay!");

            // Set up collider children
            // for (const auto entity : collidableChildren) {
            //   setupCollidableChild(entity, sharedPtr.get(), asset);
            // }
            break;
          case ModelInstancingMode::none:
            // load the model as a single object
            spdlog::trace("Loading model as single object: {}", model->getAssetPath());
            addModelToScene(modelGuid);
            break;
        }
      }

      // clear the loading instances
      assetData.loadingInstances.clear();
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////
void ModelSystem::queueModelLoad(std::shared_ptr<Model> model) {
  spdlog::trace(
    "Queueing model({}) load (instance mode: {}) -> {}", model->getGuid(),
    modelInstancingModeToString(model->getInstancingMode()), model->getAssetPath()
  );

  try {
    const auto baseAssetPath = ecs->getConfigValue<std::string>(kAssetPath);
    const auto modelAssetPath = model->getAssetPath();
    const EntityGUID modelGuid = model->getGuid();
    const ModelInstancingMode instanceMode = model->getInstancingMode();

    AssetDescriptor& assetData = _assets[modelAssetPath];

    switch (assetData.state) {
      /// Unset: not yet in queue
      case AssetLoadingState::unset:
        // mark as loading, add asset
        // assetData.path = &modelAssetPath;
        assetData.state = AssetLoadingState::loading;
        _models[modelGuid] = std::move(model);
        assetData.loadingInstances.emplace_back(modelGuid);

        spdlog::trace("  Asset unset: queued for loading.");
        loadModelFromFile(modelGuid, baseAssetPath);
        return;
      /// Loading: asset already in queue
      case AssetLoadingState::loading:
        // add model to asset's loading queue
        _models[modelGuid] = model;
        if (instanceMode == ModelInstancingMode::primary) {
          spdlog::warn("Double-load of primary model({}): {}", modelGuid, modelAssetPath);
        } else {
          assetData.loadingInstances.emplace_back(modelGuid);
        }
        spdlog::trace("  Asset loading: model queued for loading.");
        return;
      /// Loaded: asset in memory, can instance
      case AssetLoadingState::loaded:
        // add model to asset's loading queue
        _models[modelGuid] = std::move(model);
        assetData.loadingInstances.emplace_back(modelGuid);
        spdlog::trace("  Asset loaded: model queued for instancing.");
        return;
      /// Error: asset failed to load
      case AssetLoadingState::error:
        /// TODO: make sure this state is being set to begin with
        spdlog::error(
          "[ModelSystem::queueModelLoad] Asset {} failed to load, cannot "
          "queue model({})",
          modelAssetPath, modelGuid
        );
        /// TODO: actuallly handle the error somehow?
        break;
    }
  } catch (const std::exception& e) {
    throw std::runtime_error(
      "[ModelSystem::queueModelLoad] Failed to queue model load: " + std::string(e.what())
    );
  }
}

void ModelSystem::loadModelFromFile(EntityGUID modelGuid, const std::string& baseAssetPath) {
  spdlog::trace("++ loadModelFromFile");

  const auto& strand = *ecs->getStrand();
  post(strand, [&, modelGuid, baseAssetPath]() mutable {
    spdlog::trace("++ loadModelFromFile (lambda), model guid: {}", modelGuid);
    // Get model
    std::shared_ptr<Model> model = _models[modelGuid];
    if (model == nullptr) {
      throw std::runtime_error(fmt::format("[loadModelFromFile] Model {} not found", modelGuid));
    }

    try {
      const auto assetPath = model->getAssetPath();
      spdlog::trace("Loading model from assetPath: {}", assetPath);

      // Read the file and handle buffer
      const auto buffer = readBinaryFile(assetPath, baseAssetPath);
      spdlog::trace("handleFile");
      if (!buffer.empty()) {
        // Load GLB asset

        // Note if you're creating a lot of instances, this is better to use at
        // the start FilamentAsset* createInstancedAsset(const uint8_t* bytes,
        // uint32_t numBytes, FilamentInstance **instances, size_t numInstances)
        filament::gltfio::FilamentAsset* asset = nullptr;
        filament::gltfio::FilamentInstance* assetInstance = nullptr;

        asset = assetLoader_->createAsset(buffer.data(), static_cast<uint32_t>(buffer.size()));
        spdlog::trace("[loadModelFromFile] asyncBeginLoad");
        resourceLoader_->asyncBeginLoad(asset);
        model->setAsset(asset);
        _assets[assetPath].asset = asset;  // important! if not set, secondaries cannot be created

        // release source data
        if (model->getInstancingMode() == ModelInstancingMode::none) {
          spdlog::trace("[loadModelFromFile] Non-secondary loaded: releasing source "
                        "data");
          asset->releaseSourceData();  // TODO: do we also call this for
                                       // primaries after instancing?
        }

        assetInstance = asset->getInstance();
        runtime_assert(
          assetInstance != nullptr, "[loadModelFromFile] Failed to fetch primary asset instance"
        );

        model->setAssetInstance(assetInstance);

        spdlog::debug("Loaded glb model successfully from {}", assetPath);
      } else {
        spdlog::error("Couldn't load glb model from {}", assetPath);
      }

    } catch (const std::exception& e) {
      spdlog::error("[ModelSystem::loadModelFromFile] Failed to load: {}", e.what());
    } catch (...) {
      spdlog::error("[ModelSystem::loadModelFromFile] Unknown Exception in lambda");
    }
  });
}

////////////////////////////////////////////////////////////////////////////////////
void ModelSystem::onSystemInit() {
  spdlog::debug("[{}] Initializing ModelSystem", __FUNCTION__);
  // TODO: can be removed pending LifecycleParticipant impl.
  if (materialProvider_ != nullptr) {
    return;
  }

  _transforms = ecs->getSystem<TransformSystem>(__FUNCTION__);

  // Get filament
  _filament = ecs->getSystem<FilamentSystem>(__FUNCTION__);
  runtime_assert(_filament != nullptr, "ModelSystem::onSystemInit: FilamentSystem not init yet");

  _engine = _filament->getFilamentEngine();
  runtime_assert(_engine != nullptr, "ModelSystem::onSystemInit: FilamentEngine not found");

  _rcm = _engine->getRenderableManager();
  _tm = _engine->getTransformManager();
  _em = _engine->getEntityManager();

  runtime_assert(_rcm != nullptr, "ModelSystem::onSystemInit: RenderableManager not found");
  runtime_assert(_tm != nullptr, "ModelSystem::onSystemInit: TransformManager not found");
  runtime_assert(_em != nullptr, "ModelSystem::onSystemInit: EntityManager not found");

  spdlog::trace("[{}] loaded filament systems", __FUNCTION__);

  materialProvider_ = filament::gltfio::createUbershaderProvider(
    _engine, UBERARCHIVE_DEFAULT_DATA, static_cast<size_t>(UBERARCHIVE_DEFAULT_SIZE)
  );

  // new NameComponentManager(EntityManager::get());
  names_ = new ::utils::NameComponentManager(::utils::EntityManager::get());

  SPDLOG_DEBUG("UbershaderProvider MaterialsCount: {}", materialProvider_->getMaterialsCount());

  AssetConfiguration assetConfiguration{};
  assetConfiguration.engine = _engine;
  assetConfiguration.materials = materialProvider_;
  assetConfiguration.names = names_;
  assetLoader_ = AssetLoader::create(assetConfiguration);

  ResourceConfiguration resourceConfiguration{};
  resourceConfiguration.engine = _engine;
  resourceConfiguration.normalizeSkinningWeights = true;
  resourceLoader_ = new ResourceLoader(resourceConfiguration);

  const auto decoder = filament::gltfio::createStbProvider(_engine);
  resourceLoader_->addTextureProvider("image/png", decoder);
  resourceLoader_->addTextureProvider("image/jpeg", decoder);
  // TODO: add support for other texture formats here

  registerMessageHandler(ECSMessageType::ToggleVisualForEntity, [this](const ECSMessage& msg) {
    spdlog::debug("ToggleVisualForEntity");

    const auto guid = msg.getData<EntityGUID>(ECSMessageType::ToggleVisualForEntity);
    const auto value = msg.getData<bool>(ECSMessageType::BoolValue);

    if (const auto ourEntity = _models.find(guid); ourEntity != _models.end()) {
      if (const auto modelAsset = ourEntity->second->getAsset()) {
        if (value) {
          _filament->getFilamentScene()->addEntities(
            modelAsset->getRenderableEntities(), modelAsset->getRenderableEntityCount()
          );
        } else {
          _filament->getFilamentScene()->removeEntities(
            modelAsset->getRenderableEntities(), modelAsset->getRenderableEntityCount()
          );
        }
      } else if (const auto modelAssetInstance = ourEntity->second->getAssetInstance()) {
        if (value) {
          _filament->getFilamentScene()->addEntities(
            modelAssetInstance->getEntities(), modelAssetInstance->getEntityCount()
          );
        } else {
          _filament->getFilamentScene()->removeEntities(
            modelAssetInstance->getEntities(), modelAssetInstance->getEntityCount()
          );
        }
      }
    }

    spdlog::trace("ToggleVisualForEntity Complete");
  });
}

////////////////////////////////////////////////////////////////////////////////////
void ModelSystem::update(double /*deltaTime*/) {
  // Make sure all models are loaded
  updateAsyncAssetLoading();

  // Instance models that have just been loaded
  // TODO: see TODOs in `updateAsyncAssetLoading`
}

////////////////////////////////////////////////////////////////////////////////////
void ModelSystem::onDestroy() {
  destroyAllAssetsOnModels();
  delete resourceLoader_;
  resourceLoader_ = nullptr;

  if (assetLoader_) {
    AssetLoader::destroy(&assetLoader_);
    assetLoader_ = nullptr;
  }
}

////////////////////////////////////////////////////////////////////////////////////
void ModelSystem::debugPrint() { SPDLOG_DEBUG("{}", __FUNCTION__); }

}  // namespace plugin_filament_view
