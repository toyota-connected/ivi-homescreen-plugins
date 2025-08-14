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
#include "ecs.h"

// #include <spdlog/spdlog.h>
#include <asio/post.hpp>
#include <chrono>
#include <core/utils/kvtree.cc>  // NOLINT
#include <thread>

namespace plugin_filament_view {

const std::string& ECSOperationToString(ECSOperation operation) {
  static const std::unordered_map<ECSOperation, std::string> operationToString = {
    {ECSOperation::Add, "Add"}, {ECSOperation::Remove, "Remove"}
  };
  return operationToString.at(operation);
}

template class KVTree<EntityGUID, std::shared_ptr<EntityObject>>;

////////////////////////////////////////////////////////////////////////////
ECSManager* ECSManager::m_poInstance = nullptr;

////////////////////////////////////////////////////////////////////////////
ECSManager::~ECSManager() { spdlog::trace("ECSManager~"); }

////////////////////////////////////////////////////////////////////////////
ECSManager::ECSManager()
  : _entitiesMutex(),
    _entities(),
    _componentsMutex(),
    _components(),
    _systemsMutex(),
    _systems(),
    _componentListeners(),
    io_context_(std::make_unique<asio::io_context>(ASIO_CONCURRENCY_HINT_1)),
    work_(make_work_guard(io_context_->get_executor())),
    strand_(std::make_unique<asio::io_context::strand>(*io_context_)),
    m_eCurrentState(NotInitialized) {
  spdlog::trace("++ECSManager++");
  setupThreadingInternals();
}

////////////////////////////////////////////////////////////////////////////
void ECSManager::StartMainLoop() {
  if (m_bIsRunning) {
    return;
  }

  spdlog::info("\n\n\n === Starting ECS main loop ===\n");
  m_bIsRunning = true;
  m_bSpawnedThreadFinished = false;

  // Launch MainLoop in a separate thread
  loopThread_ = std::thread(&ECSManager::MainLoop, this);
}

////////////////////////////////////////////////////////////////////////////
void ECSManager::setupThreadingInternals() {
  filament_api_thread_ = std::thread([this] {
    // Save this thread's ID as it runs io_context_->run()
    filament_api_thread_id_ = pthread_self();

    // Optionally set the thread name
    pthread_setname_np(filament_api_thread_id_, "ECSManagerThreadRunner");

    spdlog::debug("ECSManager Filament API thread started: 0x{:x}", filament_api_thread_id_);

    io_context_->run();
  });
}

std::chrono::duration<double> _sleepOverhead = std::chrono::duration<double>(0.00006);  // 0.06ms

////////////////////////////////////////////////////////////////////////////
void ECSManager::MainLoop() {
  constexpr std::chrono::microseconds frameTime(long(1000000 / 60));  // ~1/60 second

  // Initialize lastFrameTime to the current time
  auto lastFrameTime = std::chrono::steady_clock::now();

  m_eCurrentState = Running;
  while (m_bIsRunning) {
    auto start = std::chrono::steady_clock::now();

    // Calculate the time difference between this frame and the last frame
    std::chrono::duration<float> elapsedTime = start - lastFrameTime;

    if (!isHandlerExecuting.load()) {
      // Use a promise to wait for the work to be done
      auto awaiter = std::promise<void>();

      // Use asio::post to schedule work on the main thread (API thread)
      post(*strand_, [elapsedTime = elapsedTime.count(), &awaiter, this]() mutable {
        isHandlerExecuting.store(true);
        try {
          // update(elapsedTime);  // Pass elapsed time to the main thread
        } catch (...) {
          isHandlerExecuting.store(false);
          // TODO: Handle exceptions properly
          // throw;  // Rethrow the exception after resetting the flag
          awaiter.set_exception(std::current_exception());
        }
        isHandlerExecuting.store(false);

        // Notify that the work is done
        awaiter.set_value();
      });

      // Wait for the work to be done
      try {
        awaiter.get_future().wait();
      } catch (const std::exception& e) {
        spdlog::error("Exception in ECSManager main loop: {}", e.what());
        // Handle the exception as needed
      }
    }

    // Update the time for the next frame
    lastFrameTime = start;

    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    spdlog::debug("[ECS] Frametime: {:.2f} ms", elapsed.count() * 1000.0);

    // Sleep for the remaining time in the frame
    if (elapsed < frameTime) {
      auto intendedSleepDuration = frameTime - elapsed;

      auto sleepStart = std::chrono::steady_clock::now();
      std::this_thread::sleep_for(intendedSleepDuration - _sleepOverhead);  // 0.06ms sleep overhead
      std::chrono::duration<double> sleepDuration = std::chrono::steady_clock::now() - sleepStart;

      _sleepOverhead = sleepDuration - intendedSleepDuration
                       + _sleepOverhead;  // Update the overhead for next time
      _sleepOverhead = std::clamp(
        _sleepOverhead, std::chrono::duration<double>(-1), std::chrono::duration<double>(1)
      );

      // spdlog::debug("Sleep overhead: {:.2f} ms", _sleepOverhead.count() * 1000.0);
      // spdlog::debug("Sleep: {:.2f} ms", sleepDuration.count() * 1000.0);
      // spdlog::debug("Total frame time: {:.2f} ms", (elapsed + sleepDuration).count() * 1000.0);
    }
  }
  m_eCurrentState = ShutdownStarted;

  m_bSpawnedThreadFinished = true;
}

////////////////////////////////////////////////////////////////////////////
void ECSManager::StopMainLoop() {
  m_bIsRunning = false;
  if (loopThread_.joinable()) {
    loopThread_.join();
  }

  // Stop the io_context
  io_context_->stop();

  // reset the work guard
  work_.reset();

  // Join the filament_api_thread_
  if (filament_api_thread_.joinable()) {
    filament_api_thread_.join();
  }
}

//
//  Entity
//
void ECSManager::checkHasEntity(EntityGUID id) {
  std::lock_guard lock(_entitiesMutex);

  spdlog::trace("[{}] Checking if entity with id {} exists", __FUNCTION__, id);
  if (id == kNullGuid || _entities.get(id) == nullptr) {
    throw std::runtime_error(fmt::format("[{}] Unable to find entity with id {}", __FUNCTION__, id)
    );
  }
}

void ECSManager::addEntity(const std::shared_ptr<EntityObject>& entity, const EntityGUID* parent) {
  {  // lock scope
    std::lock_guard lock(_entitiesMutex);
    const EntityGUID id = entity->getGuid();
    if (_entities.get(id)) {
      spdlog::error("[{}] Entity with GUID {} already exists", __FUNCTION__, id);
      return;
    }

    _entities.insert(id, entity, parent);
  }  // unlock here (entity init will lock again)

  // initialize the entity
  entity->initialize(this);
}

void ECSManager::removeEntity(const EntityGUID id) {
  // EntityObject* entity = getEntity(id).get();

  std::lock_guard lock(_entitiesMutex);

  // remove all components belonging to this entity
  for (auto& [componentId, componentMap] : _components) {
    auto it = componentMap.find(id);
    if (it != componentMap.end()) {
      // TODO: destroy the component before removing it
      componentMap.erase(it);
    }
  }

  // remove the entity from the tree
  _entities.remove(id);

  // TODO: make sure all children are removed too
}

std::shared_ptr<EntityObject> ECSManager::getEntity(EntityGUID id) {
  std::lock_guard lock(_entitiesMutex);

  const auto* node = _entities.get(id);
  if (!node) {
    spdlog::error(
      "[{}] Unable to find "
      "entity with id {}",
      __FUNCTION__, id
    );
    return nullptr;
  }
  return *(node->getValue());
}

void ECSManager::reparentEntity(
  const std::shared_ptr<EntityObject>& entity,
  const EntityGUID& parentGuid
) {
  try {
    _entities.reparent(entity->getGuid(), &parentGuid);
  } catch (const std::runtime_error& e) {
    spdlog::error("[ECSManager::ReparentEntityObject] {}", e.what());
  }
}

std::vector<EntityGUID> ECSManager::getEntityChildrenGuids(EntityGUID id) {
  const auto* node = _entities.get(id);
  if (!node) {
    spdlog::error("[{}] Unable to find entity with id {}", __FUNCTION__, id);
    return {};
  }

  std::vector<KVTreeNode<EntityGUID, std::shared_ptr<EntityObject>>*> childrenNodes =
    node->getChildren();

  std::vector<EntityGUID> childrenGuids;
  childrenGuids.reserve(childrenNodes.size());
  for (const auto* childNode : childrenNodes) {
    childrenGuids.push_back(childNode->getKey());
  }

  return childrenGuids;
}

std::vector<std::shared_ptr<EntityObject>> ECSManager::getEntityChildren(EntityGUID id) {
  std::vector<EntityGUID> childrenGuids = getEntityChildrenGuids(id);

  std::vector<std::shared_ptr<EntityObject>> children;
  children.reserve(childrenGuids.size());
  for (const auto& childGuid : childrenGuids) {
    const auto child = getEntity(childGuid);
    if (child) {
      children.push_back(child);
    }
  }

  return children;
}

std::optional<EntityGUID> ECSManager::getEntityParentGuid(EntityGUID id) {
  const auto* node = _entities.get(id);
  if (!node) {
    spdlog::error(
      "[ECSManager::getEntityParentGuid] Unable to find "
      "entity with id {}",
      id
    );
    return std::nullopt;
  }

  const auto* parentNode = node->getParent();
  if (!parentNode) {
    return std::nullopt;
  }

  return parentNode->getKey();
}

std::vector<std::shared_ptr<EntityObject>> ECSManager::getEntitiesWithComponent(
  TypeID componentTypeId
) {
  std::unique_lock lock(_componentsMutex);
  std::vector<std::shared_ptr<EntityObject>> entitiesWithComponent;

  auto componentMap = _components[componentTypeId];
  for (const auto& [entityGuid, component] : componentMap) {
    auto entity = getEntity(entityGuid);
    if (entity) {
      entitiesWithComponent.emplace_back(entity);
    }
  }

  return entitiesWithComponent;
}

//
// Component
//

std::shared_ptr<Component> ECSManager::getComponent(
  const EntityGUID& entityGuid,
  TypeID componentTypeId
) {
  std::unique_lock lock(_componentsMutex);
  auto componentMap = _components[componentTypeId];
  auto it = componentMap.find(entityGuid);
  if (it != componentMap.end()) {
    return it->second;
  } else {
    return nullptr;
  }
}

std::vector<std::shared_ptr<Component>> ECSManager::getComponentsOfType(TypeID componentTypeId) {
  std::unique_lock lock(_componentsMutex);
  std::vector<std::shared_ptr<Component>> componentsOfType;

  /// TODO: this does a memcopy, nuh-uh
  auto componentMap = _components[componentTypeId];
  for (const auto& [entityGuid, component] : componentMap) {
    componentsOfType.emplace_back(component);
  }

  return componentsOfType;
}

bool ECSManager::hasComponent(const EntityGUID entityGuid, TypeID componentTypeId) {
  checkHasEntity(entityGuid);

  std::unique_lock lock(_componentsMutex);
  auto componentMap = _components[componentTypeId];
  return componentMap.find(entityGuid) != componentMap.end();
}

void ECSManager::addComponent(
  const EntityGUID entityGuid,
  const std::shared_ptr<Component>& component
) {
  // Check if the entity exists
  checkHasEntity(entityGuid);
  EntityObject* entity = getEntity(entityGuid).get();

  // Add the component to the map
  const TypeID componentId = component->getTypeID();
  // std::unique_lock lock(_componentsMutex);

  if (_components.find(componentId) == _components.end()) {
    _components[componentId] = std::map<EntityGUID, std::shared_ptr<Component>>();
  }

  auto& componentMap = _components[componentId];

  // Check if the component already exists for this entity
  if (componentMap[entityGuid]) {
    spdlog::warn(
      "[{}] Component '{}' already exists for entity({}), overwriting",  //
      __FUNCTION__, component->getTypeName(), entityGuid
    );
  }

  // Add the component to the entity
  componentMap[entityGuid] = component;
  entity->onAddComponent(component);
  spdlog::debug(
    "[{}] Added component {} to entity with id {}", __FUNCTION__, component->getTypeName(),
    entityGuid
  );

  // Notify listeners
  _notifyComponentOperation(*entity, *component, ECSOperation::Add);
}

void ECSManager::_notifyComponentOperation(
  EntityObject& entity,
  Component& component,
  ECSOperation operation
) {
  spdlog::debug("[{}] Notifying component operation: {} ", __FUNCTION__, ECSOperationToString(operation));

  const auto systems = _componentListeners.find(component.getTypeID());
  if (systems == _componentListeners.end()) {
    return;
  }

  for (const auto& systemId : systems->second) {
    auto system = getSystem(systemId, __FUNCTION__);
    if (!system) {
      spdlog::warn(
        "System '{}'({}) not found for component '{}' operation", systemId, component.getTypeID()
      );
      continue;
    }

    spdlog::debug(
      "Notifying system '{}'({})"
      "of component '{}'()"
      "operation: {}"
      "on entity '{}'({})",
      system->getTypeName(), systemId,                 //
      component.getTypeName(), component.getTypeID(),  //
      ECSOperationToString(operation),                 //
      entity.name, entity.getGuid()                    //
    );

    system->onComponentOperation(entity, component, operation);
  }
}

std::vector<std::shared_ptr<Component>> ECSManager::getComponentsOfEntity(
  const EntityGUID& entityGuid
) {
  std::unique_lock lock(_componentsMutex);
  std::vector<std::shared_ptr<Component>> entityComponents;

  for (const auto& [componentId, componentMap] : _components) {
    auto it = componentMap.find(entityGuid);
    if (it != componentMap.end()) {
      entityComponents.push_back(it->second);
    }
  }

  return entityComponents;
}

void ECSManager::removeComponent(const EntityGUID& entityGuid, TypeID componentTypeId) {
  std::unique_lock lock(_componentsMutex);
  auto componentMap = _components[componentTypeId];
  componentMap.erase(entityGuid);
}

//
//  System
//

void ECSManager::initialize() {
  // Note this is currently expected to be called from within
  // an already asio post, Leaving this commented out so you know
  // that you could change up the routine, but if you do
  // it needs to run on the main thread.
  // asio::post(*ECSManager::GetInstance()->getStrand(), [&] {
  std::string systemName = "(null)";
  for (auto& [systemId, system] : _systems) {
    try {
      systemName = system->getTypeName();
      spdlog::debug(
        "Initializing system {} ({}) at address {}", systemName, systemId,
        static_cast<void*>(system.get())
      );

      system->initialize(*const_cast<const ECSManager*>(this));
    } catch (const std::exception& e) {
#ifndef CRASH_ON_INIT
      spdlog::error("Failed to initialize system {} ({}): {}", systemName, systemId, e.what());
#else
      throw std::runtime_error(
        fmt::format("Failed to initialize system {} ({}): {}", systemName, systemId, e.what())
      );
#endif
    }
  }

  spdlog::info("All systems initialized");
  m_eCurrentState = Initialized;
}

std::shared_ptr<System> ECSManager::getSystem(TypeID systemTypeID, const std::string& where) {
  if (const auto callingThread = pthread_self(); callingThread != filament_api_thread_id_) {
    // Note we should have a 'log once' base functionality in common
    // creating this inline for now.
    if (const auto foundIter = m_mapOffThreadCallers.find(where);
        foundIter == m_mapOffThreadCallers.end()) {
      spdlog::info(
        "From {} "
        "You're calling to get a system from an off thread, undefined "
        "experience!"
        " Use a message to do your work or grab the ecsystemmanager strand "
        "and "
        "do your work.",
        where
      );

      m_mapOffThreadCallers.insert(std::pair(where, 0));
    }
  }

  std::unique_lock lock(_systemsMutex);
  auto it = _systems.find(systemTypeID);
  if (it != _systems.end()) {
    return it->second;  // Return the found system
  } else {
    return nullptr;     // If no matching system found
  }
}

void ECSManager::addSystem(const std::shared_ptr<System>& system) {
  std::unique_lock lock(_systemsMutex);
  const TypeID systemId = system->getTypeID();

  // Check if the system is already registered
  if (_systems.find(systemId) != _systems.end()) {
    throw std::runtime_error(
      fmt::format("System {} ({}) is already registered", system->getTypeName(), systemId)
    );
  }

  spdlog::trace(
    "Adding system {} ({}) at address {}", system->getTypeName(), systemId,
    static_cast<void*>(system.get())
  );

  _systems[systemId] = system;
}

void ECSManager::removeSystem(TypeID systemTypeId) {
  std::unique_lock lock(_systemsMutex);

  auto system = getSystem(systemTypeId, __FUNCTION__);
  system->onDestroy();
  _systems.erase(systemTypeId);

  spdlog::trace(
    "Removed system {} ({}) at address {}",  //
    system->getTypeName(), systemTypeId, static_cast<void*>(system.get())
  );
}

void ECSManager::registerComponentListener(const System& listener, TypeID componentTypeId) {
  spdlog::debug("Registering component listener for component type {}", componentTypeId);
  // Create a vector if not present
  if (_componentListeners.find(componentTypeId) == _componentListeners.end()) {
    _componentListeners[componentTypeId] = std::vector<TypeID>();
  }

  _componentListeners[componentTypeId].push_back(listener.getTypeID());
}

////////////////////////////////////////////////////////////////////////////
void ECSManager::update(const double deltaTime) {
  // Copy systems under mutex
  std::map<TypeID, std::shared_ptr<System>> systemsCopy;
  {
    std::unique_lock lock(_systemsMutex);

    // Copy the systems map
    systemsCopy = _systems;
  }  // Mutex is unlocked here

  // Iterate over the copy without holding the mutex
  for (auto& [id, system] : systemsCopy) {
    if (system) {
      system->ProcessMessages();
      system->update(deltaTime);
    } else {
      spdlog::error("Encountered null system pointer!");
    }
  }
}

////////////////////////////////////////////////////////////////////////////
void ECSManager::debugPrint() const {
  for (auto& [id, system] : _systems) {
    spdlog::debug(
      "[{}] system {} at address {}, "
      "use_count={}",
      __FUNCTION__, system->getTypeName(), static_cast<void*>(system.get()), system.use_count()
    );
  }
}

////////////////////////////////////////////////////////////////////////////
void ECSManager::destroy() {
  post(*this->getStrand(), [&] {
    // we shutdown in reverse, until we have a 'system dependency tree' type of
    // view, filament system (which is always the first system, needs to be
    // shutdown last as its 'engine' varible is used in destruction for other
    // systems
    // for (auto& [id, system] : _systems) {
    //   (*it)->onDestroy();
    // }
    for (auto it = _systems.rbegin(); it != _systems.rend(); ++it) {
      const auto& id = it->first;
      const auto& system = it->second;

      if (system) {
        spdlog::trace(
          "Shutting down system {} ({}) at address {}", system->getTypeName(), id,
          static_cast<void*>(system.get())
        );

        removeSystem(id);  // Remove the system from the map
      } else {
        spdlog::error("Encountered null system pointer!");
      }
    }

    m_eCurrentState = Shutdown;
  });
}

}  // namespace plugin_filament_view
